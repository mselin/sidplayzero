// SPDX-License-Identifier: GPL-3.0-or-later
// spz_tool (tool.h): the commands of sound: `audio-guard` (SPEC-022), which proves the guard of the audio
// callback, and `load-tunes` (SPEC-203), which gives tunes to the real engine through the software backend.
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "app/hooks/tool.h"
#include "platform/audio_guard.h"
#include "platform/miniaudio_sink.h"
#include "platform/real_engine.h"
#include "platform/software_sound.h"
#include "spz/path_text.h"
#include "spz/tune.h"

namespace spz::tool {

// --- audio-guard (SPEC-022) -------------------------------------------------------------------------------

namespace {

// A fake engine: a ramp, as the unit tests' fake gives.
class RampEngine : public Engine {
public:
    bool load(const std::vector<std::uint8_t>&, int, const EngineSettings&, std::string*) override {
        at_ = 0;
        return true;
    }
    std::size_t render(std::int16_t* out, std::size_t frames) override {
        for (std::size_t i = 0; i < frames; ++i) {
            const auto value = static_cast<std::int16_t>((at_ + i) % 32000);
            out[2 * i] = value;
            out[2 * i + 1] = static_cast<std::int16_t>(-value);
        }
        at_ += frames;
        return frames;
    }
    int chips() const override { return 1; }
    void set_muted(int, int, bool) override {}
    void set_filter(int, bool) override {}
    bool registers(int, std::uint8_t out[kRegisters]) override {
        std::memset(out, 0, kRegisters);
        return true;
    }

private:
    std::uint64_t at_ = 0;
};

// The real sink, with the test's misdeed added to the callback it is given: inside the guard, as the
// backend's own callback is.
class MisbehavingSink : public AudioSink {
public:
    MisbehavingSink(AudioSink* real, std::string how) : real_(real), how_(std::move(how)) {}
    bool open(unsigned rate, Pull pull) override {
        return real_->open(rate, [this, pull](std::int16_t* out, std::size_t frames) {
            pull(out, frames);
            const int call = ++callbacks;
            if (call != 3) return;
            if (how_ == "allocate") {
                static void* (*volatile allocate)(std::size_t) = std::malloc;
                void* memory = allocate(64);
                std::free(memory);
            } else if (how_ == "lock") {
                static std::mutex mutex;
                mutex.lock();
                mutex.unlock();
            }
        });
    }
    void close() override { real_->close(); }
    void on_device_lost(std::function<void()> fn) override { real_->on_device_lost(std::move(fn)); }
    std::atomic<int> callbacks{0};

private:
    AudioSink* real_;
    std::string how_;
};

}  // namespace

int audio_guard(const std::vector<std::string>& args) {
    if (args.size() != 1 || (args[0] != "clean" && args[0] != "allocate" && args[0] != "lock")) {
        return usage("audio-guard clean | allocate | lock");
    }
    if (!platform::audio_guard_built()) {
        say("guard", "not in this build: it is in a test build without a sanitizer");
        return 1;
    }
    RampEngine engine;
    platform::MiniaudioSink real_sink(platform::MiniaudioSink::Output::Null);
    MisbehavingSink sink(&real_sink, args[0]);
    platform::RealClock clock;
    app::UiQueue ui;
    platform::SoftwareSound sound(&engine, &sink, &clock, &ui);
    SoundBackend* backend = sound.backend();
    EngineSettings settings;
    std::string why;
    if (!backend->load(std::vector<std::uint8_t>(), 1, settings, &why)) {
        say("load", "failed " + why);
        return 1;
    }
    backend->start();
    sound.start_render_thread();
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    run_ui(&ui, &clock, [&] { return std::chrono::steady_clock::now() >= until; });
    sound.stop_render_thread();
    backend->stop();
    real_sink.close();
    ui.run_all();
    say("callbacks", std::to_string(sink.callbacks.load()));
    return 0;
}

// --- load-tunes (SPEC-203) --------------------------------------------------------------------------------

namespace {

// A sink that opens and is never pulled: the render is all that is wanted.
class QuietSink : public AudioSink {
public:
    bool open(unsigned, Pull) override { return true; }
    void close() override {}
    void on_device_lost(std::function<void()>) override {}
};

// What the player does with a tune file: SPEC-203's check, then, if it passes, load() of the real software
// backend with the real engine, and 100 ms rendered. "played", "refused MSG_..." or "failed MSG_...".
// With `unchecked`, the player's check is left out, and the file goes to the backend as it is: what the
// real engine does with it on its own.
std::string load_one(const std::vector<std::uint8_t>& bytes, bool unchecked = false) {
    const std::string refused = unchecked ? std::string() : check_tune(bytes);
    if (!refused.empty()) return "refused " + refused;
    TuneInfo info;
    read_tune_info(bytes, &info);
    platform::RealEngine engine;
    QuietSink sink;
    platform::RealClock clock;
    app::UiQueue ui;
    std::string result = "played";
    {
        platform::SoftwareSound sound(&engine, &sink, &clock, &ui);
        SoundBackend* backend = sound.backend();
        EngineSettings settings;   // sidlite at 48,000 Hz, the model and clock from the header
        std::string why;
        if (!backend->load(bytes, info.default_song > 0 ? info.default_song : 1, settings, &why)) {
            result = "failed " + (why.empty() ? std::string("MSG_ENGINE_REFUSED") : why);
        } else {
            backend->start();
            const std::size_t wanted = settings.sample_rate / 10;
            for (int turn = 0; turn < 10000 && sound.buffered() < wanted; ++turn) {
                if (!sound.render_step()) break;
                ui.run_all();
            }
            backend->stop();
        }
        ui.run_all();
    }
    return result;
}

}  // namespace

int load_tunes(const std::vector<std::string>& args) {
    bool cut = false;
    bool unchecked = false;
    std::vector<std::string> paths;
    for (const std::string& word : args) {
        if (word == "--cut") {
            cut = true;
        } else if (word == "--unchecked") {
            unchecked = true;
        } else {
            paths.push_back(word);
        }
    }
    if (paths.empty()) return usage("load-tunes [--cut] [--unchecked] FILE...");
    RealFiles files;
    int done = 0;
    for (const std::string& path : paths) {
        std::vector<std::uint8_t> bytes;
        const FileStatus read = files.read(path, 16 * 1024 * 1024, &bytes);
        if (read != FileStatus::Ok) {
            std::fprintf(stderr, "spz_tool load-tunes: cannot read %s\n", path.c_str());
            return 1;
        }
        if (!cut) {
            say(path_text::last_part(path), load_one(bytes, unchecked));
        } else {
            int refused = 0, failed = 0, played = 0;
            for (std::size_t length = 0; length <= bytes.size(); ++length) {
                const std::string got = load_one(std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(length)), unchecked);
                if (got == "played") {
                    ++played;
                } else if (got.compare(0, 7, "refused") == 0) {
                    ++refused;
                } else {
                    ++failed;
                }
            }
            say(path_text::last_part(path), std::to_string(bytes.size() + 1) + " lengths, " + std::to_string(refused) + " refused, " +
                                   std::to_string(failed) + " failed, " + std::to_string(played) + " played");
        }
        ++done;
    }
    say("engine_sanitizer", platform::engine_sanitizer());
    say("done", std::to_string(done));
    return 0;
}

}  // namespace spz::tool
