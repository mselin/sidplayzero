// SPDX-License-Identifier: GPL-3.0-or-later
// The composition, the self-test (SPEC-260) and the measurements the app takes of itself (SPEC-240,
// SPEC-241, SPEC-112).
//
// The device script asks for a step by writing `selftest.request` in the state folder. While that file is
// there, every measurement is appended to `selftest.results` beside it as it is taken (Metrics::on_taken),
// and the step asked for runs at this start:
//
//   keys       the self-test screen, at the place `key-step`: 67 keys asked for in turn, each answer
//              compared with SPEC-111 and SPEC-099; `key_codes_wrong`.
//   engines    the self-test screen; SPEC-072's measurement of the two engines, and the built-in tune
//              rendered twice with each: the speed factors and the three `render_` keys.
//   benchmark  the set of SPEC-242 fetched (on the self-test screen) if it is absent; then played round
//              and round on the ordinary screens for 30 minutes (or `benchmark_seconds` in a build with
//              the test hooks), while the key script presses keys through App::key(), as the keyboard
//              does; then the index built again while a tune plays; every key of SPEC-240 it produces,
//              the `bench_` keys, and the self-test screen again.
//   none       nothing of its own.
//
// Each step ends with `step_done = STEP` and "Done. Hold ESC to leave." on the self-test screen.
#include <sys/resource.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "app/composition.h"
#include "platform/software_sound.h"
#include "spz/engine_choice.h"
#include "spz/keymap.h"
#include "spz/keys.h"
#include "spz/version.h"
#include "ui/selftest_screen.h"

namespace spz::app {

namespace {

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

// SPEC-260's key step: SPEC-111's names row by row, left to right, then six characters.
std::vector<std::string> asked_keys() {
    std::vector<std::string> keys = {"Esc", "Tab", "Enter", "Backspace", "Space"};
    for (char c = 'A'; c <= 'Z'; ++c) keys.push_back(std::string(1, c));
    for (char c = '0'; c <= '9'; ++c) keys.push_back(std::string(1, c));
    for (const char* name : {"Shift", "Up", "Down", "Left", "Right", "Home", "End", "PageUp", "PageDown", "Delete",
                             "PlayPause", "Previous", "Next", "Mute", "VolumeDown", "VolumeUp", "BrightnessDown",
                             "BrightnessUp", "Help", "PrintScreen"}) {
        keys.push_back(name);
    }
    for (const char* sign : {"/", ".", "_", ":", "~", "-"}) keys.push_back(sign);
    return keys;
}

// Whether `code` is the right answer for the key asked for: the code SPEC-111 gives its name, or for one of
// the six characters the code SPEC-099 gives it.
bool right_code(const std::string& asked, int code) {
    const Key key = key_from_name(asked);
    if (key != Key::None) return key_code(key) == code;
    if (asked.size() != 1) return false;
    bool shift = false;
    return code_for_character(static_cast<char32_t>(static_cast<unsigned char>(asked[0])), &shift) == code;
}

std::string decimal(double value, int places) {
    if (!(value >= 0) || std::isinf(value)) value = 0;
    char text[64];
    std::snprintf(text, sizeof text, "%.*f", places, value);
    return text;
}

std::string trim(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos) return "";
    return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

std::string file_name(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// "3:55.594": a length as the song length database writes it.
std::string length_text(Millis ms) {
    char text[32];
    std::snprintf(text, sizeof text, "%lld:%02lld.%03lld", static_cast<long long>(ms / 60000),
                  static_cast<long long>(ms / 1000 % 60), static_cast<long long>(ms % 1000));
    return text;
}

// ---- SHA-256 (FIPS 180-4), for render_hash_accurate and render_hash_light --------------------------------

class Sha256 {
public:
    void add(const std::uint8_t* data, std::size_t size) {
        for (std::size_t i = 0; i < size; ++i) {
            block_[used_++] = data[i];
            if (used_ == 64) {
                compress();
                used_ = 0;
            }
        }
        bytes_ += size;
    }
    std::string hex() {
        const std::uint64_t bits = bytes_ * 8;
        const std::uint8_t one = 0x80, zero = 0;
        add(&one, 1);
        while (used_ != 56) add(&zero, 1);
        std::uint8_t length[8];
        for (int i = 0; i < 8; ++i) length[i] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
        add(length, 8);
        std::string text;
        char digits[9];
        for (const std::uint32_t word : h_) {
            std::snprintf(digits, sizeof digits, "%08x", word);
            text += digits;
        }
        return text;
    }

private:
    static std::uint32_t rotate(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    void compress() {
        static const std::uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = static_cast<std::uint32_t>(block_[4 * i]) << 24 | static_cast<std::uint32_t>(block_[4 * i + 1]) << 16 |
                   static_cast<std::uint32_t>(block_[4 * i + 2]) << 8 | static_cast<std::uint32_t>(block_[4 * i + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotate(w[i - 15], 7) ^ rotate(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotate(w[i - 2], 17) ^ rotate(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t s1 = rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25);
            const std::uint32_t choose = (e & f) ^ (~e & g);
            const std::uint32_t t1 = h + s1 + choose + k[i] + w[i];
            const std::uint32_t s0 = rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = s0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d; h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += h;
    }

    std::uint32_t h_[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::uint8_t block_[64] = {};
    std::size_t used_ = 0;
    std::uint64_t bytes_ = 0;
};

// The built-in tune, rendered for ten seconds from a fresh load: its samples, as 16-bit little-endian
// stereo. Empty if the engine refused it.
std::vector<std::uint8_t> render_builtin(Engine* engine, EngineKind kind, int rate) {
    EngineSettings settings;
    settings.kind = kind;
    settings.sample_rate = static_cast<unsigned>(rate);
    std::string why;
    if (!engine->load(builtin_tune(), 1, settings, &why)) return {};
    const std::size_t frames = static_cast<std::size_t>(kRenderSeconds * rate);
    std::vector<std::int16_t> samples(frames * 2);
    std::size_t done = 0;
    while (done < frames) {
        const std::size_t got = engine->render(samples.data() + 2 * done, std::min<std::size_t>(frames - done, 4096));
        if (got == 0) return {};
        done += got;
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(samples.size() * 2);
    for (const std::int16_t sample : samples) {
        const std::uint16_t value = static_cast<std::uint16_t>(sample);
        bytes.push_back(static_cast<std::uint8_t>(value & 0xff));
        bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    }
    return bytes;
}

// The processor time of the whole process, all threads, in milliseconds.
double cpu_ms() { return 1000.0 * static_cast<double>(std::clock()) / CLOCKS_PER_SEC; }

// The process's peak resident size, in KiB.
long peak_memory_kb() {
    rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
    return usage.ru_maxrss;
}

}  // namespace

// ---- the self-test ------------------------------------------------------------------------------------------

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
    void write(const std::string& key, const std::string& value) {
        if (!recording) return;
        const std::string line = key + " = " + value + "\n";
        std::lock_guard<std::mutex> lock(write_lock);
        parts->seams_.files->append(results, Bytes(line.begin(), line.end()));
    }

    // The measurements that belong to no step, as Metrics holds them now (SPEC-260: at every exit, and here
    // at the start, so that a device script that looks before the app has left finds them).
    void write_unstepped() {
        for (const MetricRow& row : metric_table()) {
            const std::string key = row.key;
            if (row.when == MetricWhen::WhenTaken || key == "start_ms" || key == "exit_ms") continue;
            if (parts->metrics_.has(key)) write(key, parts->metrics_.value(key));
        }
    }

    void say(std::vector<std::string> lines) {
        shown = std::move(lines);
        parts->app()->redraw();
    }

    void done(const std::string& what, std::vector<std::string> lines) {
        write("step_done", what);
        lines.push_back(ui::kSelfTestDone);
        key_step = false;
        say(std::move(lines));
    }

    void open_screen() {
        App* app = parts->app();
        if (app->top_name() != "selftest") app->open("selftest");
        app->redraw();
    }

    // ---- the key step -------------------------------------------------------------------------------------
    void key_lines() {
        std::vector<std::string> lines = {"Key step: press each key the screen asks for.",
                                          "A held ESC leaves at any time."};
        lines.push_back("Answered: " + std::to_string(next_key) + ", wrong: " + std::to_string(wrong.size()));
        lines.push_back("Key " + std::to_string(next_key + 1) + "/" + std::to_string(asked.size()) + ": " + asked[next_key]);
        say(lines);
    }

    void start_keys() {
        asked = asked_keys();
        next_key = 0;
        wrong.clear();
        key_step = true;
        open_screen();
        key_lines();
    }

    void record_key(const KeyEvent& event) {
        if (!key_step || next_key >= asked.size()) return;
        if (!right_code(asked[next_key], event.code)) wrong.push_back(asked[next_key]);
        ++next_key;
        if (next_key < asked.size()) {
            key_lines();
            return;
        }
        std::string value;
        for (const std::string& name : wrong) value += (value.empty() ? "" : " ") + name;
        if (value.empty()) value = "none";
        parts->metrics_.set("key_codes_wrong", value);
        done("keys", {"Key step: " + std::to_string(asked.size()) + " keys asked for.", "Wrong: " + value});
    }

    // ---- the engines ------------------------------------------------------------------------------------------
    void start_engines() {
        open_screen();
        say({"Engines: measuring how fast each one renders."});
        Composition* p = parts;
        auto make = p->seams_.make_engine;
        if (!make) {
            done("engines", {"Engines: this build has no engine to measure."});
            return;
        }
        Clock* clock = p->seams_.clock;
        Files* files = p->seams_.files;
        const std::string cache = p->folders_.cache;
        const int rate = p->settings_->settings().sample_rate;
        Metrics* metrics = &p->metrics_;
        p->app()->long_worker()->post([p, make, clock, files, cache, rate, metrics] {
            std::unique_ptr<Engine> engine = make();
            if (!engine || !p->running()) return;
            // SPEC-072.
            SpeedFactors factors;
            factors.accurate = measure_engine(engine.get(), EngineKind::Accurate, rate, clock);
            if (!p->running()) return;
            factors.light = measure_engine(engine.get(), EngineKind::Light, rate, clock);
            factors.measured = factors.accurate > 0 && factors.light > 0;
            if (factors.measured) {
                files->make_folders(cache);
                save_speed_factors(files, cache, factors, app_version(), rate);
                metrics->set("speed_factor_accurate", decimal(factors.accurate, 2));
                metrics->set("speed_factor_light", decimal(factors.light, 2));
            }
            p->on_ui([p, factors] {
                if (factors.measured && p->player_) p->player_->set_speed_factors(factors);
                if (p->selftest_) {
                    p->selftest_->say({"Engines: reSIDfp " + decimal(factors.accurate, 2) + ", sidlite " +
                                           decimal(factors.light, 2) + " seconds a second.",
                                       "Rendering the built-in tune twice with each."});
                }
            });
            // SPEC-241, SPEC-071: ten seconds of the built-in tune, twice with each engine.
            bool repeatable = true;
            std::string hashes[2];
            const EngineKind kinds[2] = {EngineKind::Accurate, EngineKind::Light};
            for (int i = 0; i < 2 && p->running(); ++i) {
                const std::vector<std::uint8_t> first = render_builtin(engine.get(), kinds[i], rate);
                const std::vector<std::uint8_t> second = render_builtin(engine.get(), kinds[i], rate);
                if (first.empty() || first != second) repeatable = false;
                Sha256 sha;
                sha.add(first.data(), first.size());
                hashes[i] = first.empty() ? "none" : sha.hex();
            }
            if (!p->running()) return;
            metrics->set("render_repeatable", repeatable ? "yes" : "no");
            metrics->set("render_hash_accurate", hashes[0]);
            metrics->set("render_hash_light", hashes[1]);
            p->on_ui([p, factors, repeatable] {
                if (!p->selftest_) return;
                p->selftest_->done("engines", {"Engines: reSIDfp " + decimal(factors.accurate, 2) + ", sidlite " +
                                                   decimal(factors.light, 2) + " seconds a second.",
                                               std::string("The same samples twice: ") + (repeatable ? "yes" : "no")});
            });
        });
    }

    // ---- the benchmark: fetching the set ---------------------------------------------------------------------
    void start_benchmark() {
        // Which tunes of the set are on the device is asked of Files on the quick worker (SPEC-022).
        Composition* p = parts;
        Files* files = p->seams_.files;
        const std::string root = p->root_.path;
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

    void add_to_set(int n) {
        Entry entry;
        entry.path = kBenchmarkSet[n];
        set.push_back(entry);
        set_number.push_back(n + 1);
    }

    void fetch_lines() {
        std::vector<std::string> lines = {"Benchmark: fetching the four tunes of the set.",
                                          "Tune " + std::to_string(std::min<std::size_t>(fetch_at + 1, 4)) + "/4: " +
                                              file_name(kBenchmarkSet[std::min<std::size_t>(fetch_at, 3)])};
        if (!fetch_failed.empty()) lines.push_back("Not fetched: " + std::to_string(fetch_failed.size()));
        say(lines);
    }

    // One tick while the set is fetched: each tune is played once through the player, which fetches it as
    // SPEC-141 does any tune, and stopped as soon as it sounds.
    void fetch_tick() {
        Player* player = parts->player_.get();
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

    // ---- the benchmark: playing the set ----------------------------------------------------------------------
    std::int64_t render_ns() const { return parts->sound_ ? parts->sound_->render_ns() : 0; }

    void start_playing() {
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

    void play(std::size_t index, Millis now) {
        Player* player = parts->player_.get();
        if (!player || set.empty()) return;
        playing = index % set.size();
        player->play(set, playing);
        tune_began = now;
        tune_sounding = false;
        render_ns_before = render_ns();
    }

    // The tune that played has played as long as it plays: what it rendered is added to its figures.
    void tune_over(Millis now) {
        if (set.empty() || tune_began < 0) return;
        const int n = set_number[playing];
        render_ms[n] += static_cast<double>(render_ns() - render_ns_before) / 1e6;
        played_ms[n] += static_cast<double>(now - tune_began);
    }

    void play_tick(Millis now) {
        Player* player = parts->player_.get();
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

    // ---- the key script --------------------------------------------------------------------------------------
    App* app() const { return parts->app(); }
    BrowserModel* browser() const { return parts->browser_.get(); }

    void press(int code) {
        app()->key(code, true);
        app()->key(code, false);
    }
    void press(Key key) { press(key_code(key)); }

    void then(Action action) { script.push_back(std::move(action)); }
    void then_press(Key key, Millis gap = kKeyGapMs) {
        then([this, key, gap](Millis) {
            press(key);
            return Outcome{true, gap};
        });
    }

    // Waits until `ready` holds, then gives how long it took to `took`. Gives up, and drops the rest of the
    // round, after kWaitMostMs.
    void then_wait(std::function<bool()> ready, std::function<void(Millis)> took = nullptr) {
        auto since = std::make_shared<Millis>(-1);
        then([this, ready, took, since](Millis now) {
            if (*since < 0) *since = now;
            if (ready()) {
                if (took) took(now - *since);
                return Outcome{true, kKeyGapMs};
            }
            if (now - *since > kWaitMostMs) {
                script.clear();
                return Outcome{true, 0};
            }
            return Outcome{false, 0};
        });
    }

    bool listed(const std::string& folder) const {
        const BrowserModel* model = browser();
        return model && model->folder() == folder && model->note() != ListNote::Reading &&
               (model->count() > 0 || model->note() == ListNote::Empty);
    }

    int row_named(const std::string& name) const {
        const BrowserModel* model = browser();
        if (!model) return -1;
        for (int i = 0; i < model->count(); ++i) {
            if (model->row(i).name == name) return i;
        }
        return -1;
    }

    // Moves the highlight to the row named `name`, a key at a time; the round ends if there is none.
    void then_highlight(const std::string& name, bool rest = false) {
        auto presses = std::make_shared<int>(0);
        then([this, name, presses, rest](Millis now) {
            const BrowserModel* model = browser();
            const int target = row_named(name);
            if (!model || target < 0 || ++*presses > 2000 || app()->top_name() != "browser") {
                script.clear();
                return Outcome{true, 0};
            }
            const int at = model->highlight();
            if (at == target) return Outcome{true, rest ? 0 : kKeyGapMs};
            moved_at = now;
            const int distance = target - at;
            if (distance > 6) press(Key::PageDown);
            else if (distance < -6) press(Key::PageUp);
            else press(distance > 0 ? Key::Down : Key::Up);
            return Outcome{false, kMoveGapMs};
        });
    }

    // Opens the folder `path` from the root, as the keys would: each part highlighted, then Enter. The last
    // one's first screenful is timed (SPEC-240).
    void then_open(const std::string& path, bool timed) {
        std::string folder;
        std::size_t start = 1;
        while (start < path.size()) {
            const std::size_t slash = path.find('/', start);
            const std::string name = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
            folder += "/" + name;
            const bool last = slash == std::string::npos;
            then_highlight(name);
            then_press(Key::Enter, 0);
            if (last && timed) {
                const std::string opened = folder;
                then_wait([this, opened] { return listed(opened); },
                          [this, opened](Millis ms) {
                              Millis& most = listed_before.count(opened) ? folder_again_max : folder_first_max;
                              most = std::max(most, ms);
                              listed_before.insert(opened);
                          });
            } else {
                const std::string opened = folder;
                then_wait([this, opened] { return listed(opened); });
            }
            if (last) break;
            start = slash + 1;
        }
    }

    void then_parent(const std::string& to) {
        then_press(Key::Left, 0);
        then_wait([this, to] { return listed(to); });
    }

    // SPEC-240: Down held for 10 s; the highlight's moves give the rows a second and the longest pause.
    void then_hold() {
        struct Hold {
            Millis pressed = -1;
            int last_row = -1;
            std::vector<Millis> moves;
            bool at_end = false;
        };
        auto hold = std::make_shared<Hold>();
        then([this, hold](Millis now) {
            const BrowserModel* model = browser();
            if (!model || app()->top_name() != "browser") {
                release_held();
                script.clear();
                return Outcome{true, 0};
            }
            if (hold->pressed < 0) {
                hold->pressed = now;
                hold->last_row = model->highlight();
                held_code = key_code(Key::Down);
                app()->key(held_code, true);
                return Outcome{false, 0};
            }
            const int row = model->highlight();
            if (row != hold->last_row && !hold->at_end) {
                hold->moves.push_back(now);
                hold->last_row = row;
                if (row >= model->count() - 1) hold->at_end = true;
            }
            if (now - hold->pressed < kHoldMs) return Outcome{false, 0};
            release_held();
            hold_figures(*hold.get(), now);
            return Outcome{true, kKeyGapMs};
        });
        then_press(Key::Home);
    }

    template <typename Hold>
    void hold_figures(const Hold& hold, Millis now) {
        // The moves made by the app's own repeat, after its first delay (SPEC-045).
        std::vector<Millis> repeated;
        for (const Millis at : hold.moves) {
            if (at >= hold.pressed + kKeyRepeatDelayMs) repeated.push_back(at);
        }
        const std::vector<Millis>& used = repeated.size() >= 2 ? repeated : hold.moves;
        double rate = 0;
        Millis pause = 0;
        if (used.size() >= 2) {
            rate = 1000.0 * static_cast<double>(used.size() - 1) / static_cast<double>(std::max<Millis>(1, used.back() - used.front()));
            for (std::size_t i = 1; i < used.size(); ++i) pause = std::max(pause, used[i] - used[i - 1]);
            if (!hold.at_end) pause = std::max(pause, now - used.back());
        }
        rows_per_second_min = rows_per_second_min < 0 ? rate : std::min(rows_per_second_min, rate);
        row_pause_max = std::max(row_pause_max, pause);
    }

    void release_held() {
        if (held_code == 0) return;
        app()->key(held_code, false);
        held_code = 0;
    }

    // SPEC-240: the detail line, from when the highlight comes to rest on a tune until it shows.
    void then_rest_on(const std::string& name) {
        then([this](Millis now) {
            moved_at = now;
            return Outcome{true, 0};
        });
        then_highlight(name, true);
        if (!part_built("detail")) {
            then([](Millis) { return Outcome{true, 1000}; });
            return;
        }
        then_wait(
            [this] {
                const Json description = app()->describe();
                const Json* detail = description.get("detail");
                return detail && !detail->is_null() && !detail->scalar_text().empty();
            },
            [this](Millis) { detail_max = std::max(detail_max, script_now - moved_at); });
        then([](Millis) { return Outcome{true, 500}; });     // and rests a little longer, as a reader would
    }

    void then_rest_on_tunes() {
        then([this](Millis) {
            // Three tunes of the folder, present ones first (SPEC-260); in the order they are shown.
            const BrowserModel* model = browser();
            std::vector<std::string> chosen;
            if (model) {
                for (int pass = 0; pass < 2; ++pass) {
                    for (int i = 0; i < model->count() && chosen.size() < 3; ++i) {
                        const FolderEntry& row = model->row(i);
                        if (row.is_folder || row.present != (pass == 0)) continue;
                        chosen.push_back(row.name);
                    }
                }
            }
            // These steps go before the rest of the round.
            std::deque<Action> rest;
            rest.swap(script);
            for (const std::string& name : chosen) then_rest_on(name);
            for (Action& action : rest) script.push_back(std::move(action));
            return Outcome{true, 0};
        });
    }

    void then_type(const std::string& text) {
        for (const char c : text) {
            then([this, c](Millis) {
                bool shift = false;
                const int code = code_for_character(static_cast<char32_t>(static_cast<unsigned char>(c)), &shift);
                if (shift) app()->key(key_code(Key::Shift), true);
                if (code != 0) press(code);
                if (shift) app()->key(key_code(Key::Shift), false);
                return Outcome{true, kKeyGapMs};
            });
        }
    }

    // SPEC-240: a search has been answered when its results, or "No results", are shown.
    bool search_answered() {
        if (app()->top_name() != "search") return true;
        const Json description = app()->describe();
        const Json* list = description.get("list");
        if (list && !list->is_null()) return true;
        const Json* title = description.get("title");
        return title && title->scalar_text().find("result") != std::string::npos;
    }

    void then_query(const std::string& query, bool first) {
        then([this, query, first](Millis) {
            // Only while Search is open: in another place the letters would do other things.
            if (app()->top_name() != "search") return Outcome{true, 0};
            std::deque<Action> rest;
            rest.swap(script);
            if (!first) {
                // Back to the field, and what it holds taken away.
                then([this](Millis) {
                    const Json description = app()->describe();
                    const Json* place = description.get("place");
                    if (place && place->scalar_text() == "search-results") press(key_code(Key::D4));
                    return Outcome{true, kKeyGapMs};
                });
                for (std::size_t i = 0; i < std::string(kQueries[0]).size() + 2; ++i) then_press(Key::Backspace, kMoveGapMs);
            }
            then_type(query);
            then_press(Key::Enter, 0);
            then_wait([this] { return search_answered(); }, [this](Millis ms) { search_max = std::max(search_max, ms); });
            for (Action& action : rest) script.push_back(std::move(action));
            return Outcome{true, 0};
        });
    }

    // One round of the key script (SPEC-260).
    void plan_round() {
        const bool hold = rounds % kHoldEvery == 0;
        ++rounds;
        // From the browser at the root.
        auto tries = std::make_shared<int>(0);
        then([this, tries](Millis) {
            if (app()->top_name() == "browser") return Outcome{true, 0};
            if (++*tries > 10) {
                script.clear();
                return Outcome{true, 0};
            }
            press(Key::Esc);
            return Outcome{false, kKeyGapMs};
        });
        then([this, tries](Millis) {
            const BrowserModel* model = browser();
            if (!model || model->folder() == "/") return Outcome{true, kKeyGapMs};
            if (++*tries > 100 || app()->top_name() != "browser") {
                script.clear();
                return Outcome{true, 0};
            }
            if (model->note() != ListNote::Reading) press(Key::Left);
            return Outcome{false, kKeyGapMs};
        });
        // The first folder: open it, hold Down in it, rest on three tunes, and leave it.
        then_open(kFirstFolder, true);
        if (hold) then_hold();
        then_rest_on_tunes();
        then_parent("/MUSICIANS/B");
        then_parent("/MUSICIANS");
        // The second, from the root.
        then_parent("/");
        then_open(kSecondFolder, true);
        then_parent("/MUSICIANS");
        then_parent("/");
        // Search: two queries, and back.
        then_press(Key::D5);
        then_query(kQueries[0], true);
        then_query(kQueries[1], false);
        then([this](Millis) {
            if (app()->top_name() == "search") press(Key::Esc);
            return Outcome{true, kKeyGapMs};
        });
        // Settings, opened and closed.
        then_press(Key::Tab, 400);
        then([this](Millis) {
            if (app()->top_name() == "settings") press(Key::Esc);
            return Outcome{true, kKeyGapMs};
        });
    }

    void script_tick(Millis now) {
        if (script.empty() && (round_began < 0 || now - round_began >= kRoundEveryMs) && app()->top_name() != "selftest") {
            round_began = now;
            plan_round();
        }
        script_now = now;
        while (!script.empty() && now >= script_next) {
            // Taken off first: an action may put steps of its own in front of the rest, or end the round.
            Action action = std::move(script.front());
            script.pop_front();
            const Outcome outcome = action(now);
            script_next = now + outcome.then;
            if (!outcome.done) {
                script.push_front(std::move(action));
                return;
            }
            if (outcome.then > 0) return;
        }
    }

    // ---- the benchmark: its end --------------------------------------------------------------------------
    void end_playing(Millis now) {
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
        Index* index = p->index_.get();
        if (!index) {
            finish();
            return;
        }
        const std::uint64_t underruns = p->backend_ ? p->backend_->underruns() : 0;
        p->app()->long_worker()->post([p, index, underruns] {
            const IndexRun run = index->build(IndexReason::User, [p] { return p->running(); });
            if (!run.complete) return;
            int present = 0;
            const int count = index->count();
            for (int first = 0; first < count; first += 1000) {
                for (const IndexRecord& record : index->records(first, 1000)) present += record.present ? 1 : 0;
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

    void finish() {
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
        if (p->player_) p->player_->stop();
        std::vector<std::string> lines = {"Benchmark: the set played for " + std::to_string(benchmark_ms / 1000) + " s.",
                                          "Underruns: " + m.value("underrun_frames") + ", processor: " +
                                              m.value("cpu_percent") + " %"};
        if (!fetch_failed.empty()) lines.push_back("Not fetched: " + std::to_string(fetch_failed.size()) + " of 4");
        open_screen();
        done("benchmark", lines);
    }

    // SPEC-200's start time when the browser first answers; then the benchmark, once the player has had its
    // entry back (play_at_start(), which the first listing starts on the quick worker): it goes through the
    // quick worker after it.
    void after_first_listing() {
        Composition* p = parts;
        if (!p->first_listing_seen_) {
            p->app()->quick()->post([p] {
                p->on_ui([p] {
                    if (p->selftest_) p->selftest_->after_first_listing();
                });
            });
            return;
        }
        if (!browser_answered) {
            browser_answered = true;
            if (p->seams_.start_ms >= 0) {
                p->metrics_.set_number("start_ms", p->seams_.start_ms + p->seams_.clock->now_ms() - made_at);
            }
        }
        if (step == "benchmark") {
            p->app()->quick()->post([p] {
                p->on_ui([p] {
                    if (p->selftest_) p->selftest_->start_benchmark();
                });
            });
        }
    }

    // ---- what changes by itself --------------------------------------------------------------------------
    void tick() {
        Composition* p = parts;
        const Millis now = p->seams_.clock->now_ms();
        if (now - setup_checked >= kSettingUpCheckMs) {
            setup_checked = now;
            watch_setup(now);
        }
        switch (phase) {
            case Phase::Fetching:
                fetch_tick();
                break;
            case Phase::Playing: {
                play_tick(now);
                if (now - began >= benchmark_ms) {
                    end_playing(now);
                } else {
                    script_tick(now);
                }
                // The job mark's share, drawn again when its number changes.
                const Millis percent = share_done() * 100 / std::max<Millis>(1, benchmark_ms);
                if (percent != shown_percent) {
                    shown_percent = percent;
                    p->app()->redraw();
                }
                break;
            }
            case Phase::Indexing:
                play_tick(now);
                break;
            default:
                break;
        }
    }

    // SPEC-240: setting HVSC up, from the job's start (Enter on the notice) to its end with a listing.
    void watch_setup(Millis now) {
        const JobState job = p_jobs().state();
        if (job.kind == JobKind::Setup) {
            if (setup_began < 0) setup_began = now;
        } else if (setup_began >= 0) {
            if (parts->listing_ && parts->listing_->is_open()) {
                parts->metrics_.set("hvsc_setup_s", decimal(static_cast<double>(now - setup_began) / 1000.0, 1));
            }
            setup_began = -1;
        }
    }
    Jobs& p_jobs() { return parts->jobs_; }

    // SPEC-260: the job mark while the benchmark plays.
    bool marking() const { return phase == Phase::Playing || phase == Phase::Indexing; }
    Millis share_done() const {
        if (phase == Phase::Indexing) return benchmark_ms;
        return std::min(benchmark_ms, std::max<Millis>(0, parts->seams_.clock->now_ms() - began));
    }
};

// ---- the composition's side ------------------------------------------------------------------------------

void Composition::make_selftest() {
    selftest_.reset(new SelfTest(this));
    SelfTest* self = selftest_.get();
    self->made_at = seams_.clock->now_ms();
    const std::string request = folders_.state + "/selftest.request";
    self->results = folders_.state + "/selftest.results";
    Bytes bytes;
    if (seams_.files->read(request, kRequestLimit, &bytes) != FileStatus::Ok) return;
    self->recording = true;
    self->step = "none";
    std::string line;
    const std::string text(bytes.begin(), bytes.end());
    std::size_t at = 0;
    while (at <= text.size()) {
        const std::size_t end = text.find('\n', at);
        line = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
        const std::size_t equals = line.find('=');
        if (equals != std::string::npos) {
            const std::string key = trim(line.substr(0, equals)), value = trim(line.substr(equals + 1));
            if (key == "step") self->step = value;
#if SPZ_TEST_HOOKS
            // SPEC-253, SPEC-260: a test build may shorten the benchmark.
            if (key == "benchmark_seconds") {
                try {
                    const long long seconds = std::stoll(value);
                    if (seconds > 0) self->benchmark_ms = seconds * 1000;
                } catch (const std::exception&) {
                }
            }
#endif
        }
        if (end == std::string::npos) break;
        at = end + 1;
    }

    // Every measurement goes to the results file as it is taken; those taken already go there now, but
    // the start time, which is taken when the browser first answers.
    metrics_.on_taken([self](const std::string& key, const std::string& value) { self->write(key, value); });
    self->write_unstepped();
    services().selftest = self;

    // The key step's answers (SPEC-260): `record_key` is the key-step's only action.
    app()->on_action("record_key", [self](const KeyEvent& event) { self->record_key(event); });

    // The job mark while the benchmark plays: `job`, and `mini.job` (SPEC-147, SPEC-254).
    app()->add_describer([self](Json* d) {
        if (!self->marking()) return;
        const Millis done = self->share_done();
        Json job = Json::object();
        job.set("kind", "selftest");
        job.set("step", self->phase == SelfTest::Phase::Playing ? 1 : 2);
        job.set("text", self->phase == SelfTest::Phase::Playing ? "Playing the benchmark set" : "Building the index");
        job.set("done", static_cast<std::int64_t>(done));
        job.set("total", static_cast<std::int64_t>(self->benchmark_ms));
        d->set("job", job);
        const Json* had = d->get("mini");
        Json mini = had && !had->is_null() ? *had : Json::object();
        mini.set("job", std::string("\xE2\x86\x93 ") + std::to_string(done * 100 / std::max<Millis>(1, self->benchmark_ms)) + "%");
        d->set("mini", mini);
    });
    app()->add_top_right([self]() -> std::string {
        if (!self->marking()) return "";
        return std::string("\xE2\x86\x93 ") +
               std::to_string(self->share_done() * 100 / std::max<Millis>(1, self->benchmark_ms)) + "%";
    });
    app()->every(kTickMs, [self] { self->tick(); });
}

void Composition::selftest_at_start() {
    // In the ready job: the listing is open, and the browser is about to show its first folder.
    SelfTest* self = selftest_.get();
    if (!self || !self->recording || self->started) return;
    self->started = true;
    if (self->step == "keys") self->start_keys();
    else if (self->step == "engines") self->start_engines();
    self->after_first_listing();
}

void Composition::device_measurements() {
    // SPEC-241: what only the device can say, on the device only.
    if (!seams_.device_facts) return;
    for (const auto& fact : seams_.device_facts()) metrics_.set(fact.first, fact.second);
    if (metrics_.has("audio_backend")) {
        metrics_.set_number("audio_rate", settings_->settings().sample_rate);
        metrics_.set("audio_format", "s16 stereo");
    }
    ListedTune commando;
    if (listing_ && listing_->is_open() && listing_->by_path(kCommando, &commando)) {
        std::string lengths;
        for (std::size_t i = 0; i < commando.lengths.size() && i < 2; ++i) {
            lengths += (lengths.empty() ? "" : " ") + length_text(commando.lengths[i]);
        }
        metrics_.set("lengths_commando", lengths);
    }
}

void Composition::selftest_at_exit() {
    // SPEC-260: at every exit, the keys that belong to no step; exit_ms follows when the threads have stopped.
    device_measurements();
    if (selftest_) {
        selftest_->release_held();
        selftest_->write_unstepped();
    }
}

}  // namespace spz::app
