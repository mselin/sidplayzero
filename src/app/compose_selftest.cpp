// SPDX-License-Identifier: GPL-3.0-or-later
// The composition, the self-test (SPEC-260) and the measurements the app takes of itself (SPEC-240,
// SPEC-241, SPEC-112).
//
// The device script asks for a step by writing `selftest.request` in the state folder. While that file is
// there, every measurement is appended to `selftest.results` beside it as it is taken (Metrics::on_taken),
// and the step asked for runs at this start:
//
//   keys       the self-test screen, at the place `key-step`: 25 keys asked for in turn, each answer
//              compared with SPEC-111 and SPEC-099 and written as `key_answer` as it is given; a step
//              started again begins at the first key with no answer (FIND-029); `key_codes_wrong`.
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
//
// The self-test is one object, Composition::SelfTest (app/selftest.h). This file has the composition's side
// of it, the request, the results file, the key step and the engines; selftest_benchmark.cpp has the
// benchmark, and selftest_script.cpp its key script.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

#include "app/selftest.h"
#include "platform/software_sound.h"
#include "spz/engine_choice.h"
#include "spz/keymap.h"
#include "spz/sha256.h"
#include "spz/text.h"
#include "spz/version.h"

namespace spz::app {

namespace {

// SPEC-260's key step (FIND-029): the 19 names of SPEC-111's table pressed with fn, then the six characters
// of the sym layer (SPEC-099).
std::vector<std::string> asked_keys() {
    std::vector<std::string> keys;
    for (const char* name : {"Up", "Down", "Left", "Right", "Home", "End", "PageUp", "PageDown", "Delete", "PlayPause",
                             "Previous", "Next", "Mute", "VolumeDown", "VolumeUp", "BrightnessDown", "BrightnessUp",
                             "Help", "PrintScreen"}) {
        keys.push_back(name);
    }
    for (const char* sign : {"/", ".", "_", ":", "~", "-"}) keys.push_back(sign);
    return keys;
}

const char kKeyAnswer[] = "key_answer";
const std::size_t kResultsLimit = 64 * 1024 * 1024;

// Whether `code` is the right answer for the key asked for: the code SPEC-111 gives its name, or for one of
// the six characters the code SPEC-099 gives it.
bool right_code(const std::string& asked, int code) {
    const Key key = key_from_name(asked);
    if (key != Key::None) return key_code(key) == code;
    if (asked.size() != 1) return false;
    bool shift = false;
    return code_for_character(static_cast<char32_t>(static_cast<unsigned char>(asked[0])), &shift) == code;
}

// "3:55.594": a length as the song length database writes it.
std::string length_text(Millis ms) {
    char text[32];
    std::snprintf(text, sizeof text, "%lld:%02lld.%03lld", static_cast<long long>(ms / 60000),
                  static_cast<long long>(ms / 1000 % 60), static_cast<long long>(ms % 1000));
    return text;
}

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

}  // namespace

std::string selftest::decimal(double value, int places) {
    if (!(value >= 0) || std::isinf(value)) value = 0;
    char text[64];
    std::snprintf(text, sizeof text, "%.*f", places, value);
    return text;
}

// ---- the self-test: writing, the key step, the engines, what changes by itself -------------------------------

void Composition::SelfTest::write(const std::string& key, const std::string& value) {
    if (!recording) return;
    const std::string line = key + " = " + value + "\n";
    std::lock_guard<std::mutex> lock(write_lock);
    parts->seams_.files->append(results, Bytes(line.begin(), line.end()));
}

// The measurements that belong to no step, as Metrics holds them now (SPEC-260: at every exit, and here
// at the start, so that a device script that looks before the app has left finds them).
void Composition::SelfTest::write_unstepped() {
    for (const MetricRow& row : metric_table()) {
        const std::string key = row.key;
        if (row.when == MetricWhen::WhenTaken || key == "start_ms" || key == "exit_ms") continue;
        if (parts->metrics_.has(key)) write(key, parts->metrics_.value(key));
    }
}

void Composition::SelfTest::say(std::vector<std::string> lines) {
    shown = std::move(lines);
    parts->app()->redraw();
}

void Composition::SelfTest::done(const std::string& what, std::vector<std::string> lines) {
    write("step_done", what);
    lines.push_back(ui::kSelfTestDone);
    key_step = false;
    say(std::move(lines));
}

void Composition::SelfTest::open_screen() {
    App* app = parts->app();
    if (app->top_name() != "selftest") app->open("selftest");
    app->redraw();
}

void Composition::SelfTest::key_lines() {
    std::vector<std::string> lines = {"Key step: press each key the screen asks for.",
                                      "A held ESC leaves at any time."};
    lines.push_back("Answered: " + std::to_string(answers.size()) + ", wrong: " + std::to_string(wrong.size()));
    lines.push_back("Key " + std::to_string(next_key + 1) + "/" + std::to_string(asked.size()) + ": " + asked[next_key]);
    say(lines);
}

// The answers given so far: the `key_answer` lines of the results file, the last of each name counting.
void Composition::SelfTest::read_answers() {
    answers.clear();
    Bytes bytes;
    if (parts->seams_.files->read(results, kResultsLimit, &bytes) != FileStatus::Ok) return;
    const std::string text(bytes.begin(), bytes.end());
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(at, end - at);
        at = end + 1;
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        if (text::trim(line.substr(0, equals), text::kLineSpaces) != kKeyAnswer) continue;
        const std::string value = text::trim(line.substr(equals + 1), text::kLineSpaces);
        const std::size_t space = value.rfind(' ');
        if (space == std::string::npos) continue;
        const std::string name = value.substr(0, space), verdict = value.substr(space + 1);
        if (std::find(asked.begin(), asked.end(), name) == asked.end()) continue;
        if (verdict == "right") answers[name] = true;
        else if (verdict == "wrong") answers[name] = false;
    }
}

// Goes on to the first key with no answer, and gathers the names whose last answer was wrong, in the order
// asked. False when every key has an answer.
bool Composition::SelfTest::next_unanswered() {
    wrong.clear();
    for (const std::string& name : asked) {
        const auto found = answers.find(name);
        if (found != answers.end() && !found->second) wrong.push_back(name);
    }
    next_key = 0;
    while (next_key < asked.size() && answers.count(asked[next_key])) ++next_key;
    return next_key < asked.size();
}

void Composition::SelfTest::keys_done() {
    std::string value;
    for (const std::string& name : wrong) value += (value.empty() ? "" : " ") + name;
    if (value.empty()) value = "none";
    parts->metrics_.set("key_codes_wrong", value);
    done("keys", {"Key step: " + std::to_string(asked.size()) + " keys asked for.", "Wrong: " + value});
}

void Composition::SelfTest::start_keys() {
    asked = asked_keys();
    read_answers();
    key_step = true;
    open_screen();
    if (next_unanswered()) key_lines();
    else keys_done();
}

void Composition::SelfTest::record_key(const KeyEvent& event) {
    if (!key_step || next_key >= asked.size()) return;
    const std::string& name = asked[next_key];
    const bool right = right_code(name, event.code);
    answers[name] = right;
    parts->metrics_.set(kKeyAnswer, name + (right ? " right" : " wrong"));
    if (next_unanswered()) key_lines();
    else keys_done();
}

void Composition::SelfTest::start_engines() {
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
            if (factors.measured) p->set_speed_factors(factors);
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
            hashes[i] = first.empty() ? "none" : sha256_hex(first);
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

// SPEC-200's start time when the browser first answers; then the benchmark, once the player has had its
// entry back (play_at_start(), which the first listing starts on the quick worker): it goes through the
// quick worker after it.
void Composition::SelfTest::after_first_listing() {
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

void Composition::SelfTest::tick() {
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
void Composition::SelfTest::watch_setup(Millis now) {
    const JobState job = p_jobs().state();
    if (job.kind == JobKind::Setup) {
        if (setup_began < 0) setup_began = now;
    } else if (setup_began >= 0) {
        if (parts->scope_->listing && parts->scope_->listing->is_open()) {
            parts->metrics_.set("hvsc_setup_s", decimal(static_cast<double>(now - setup_began) / 1000.0, 1));
        }
        setup_began = -1;
    }
}

Millis Composition::SelfTest::share_done() const {
    if (phase == Phase::Indexing) return benchmark_ms;
    return std::min(benchmark_ms, std::max<Millis>(0, parts->seams_.clock->now_ms() - began));
}

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
            const std::string key = text::trim(line.substr(0, equals), text::kLineSpaces), value = text::trim(line.substr(equals + 1), text::kLineSpaces);
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
    ListedTune commando;
    if (scope_->listing && scope_->listing->is_open() && scope_->listing->by_path(kCommando, &commando)) {
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
