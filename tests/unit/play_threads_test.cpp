// SPDX-License-Identifier: GPL-3.0-or-later
// The player and the software backend where threads meet (SPEC-022): the backend never makes another
// thread wait for its engine or its device, never calls back once it has gone, and opens a device again
// on a worker; and a player another has taken the backend from leaves it alone (SPEC-025).
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "play_support.h"
#include "spz/software_backend.h"
#include "support.h"

using namespace spz::test;
using spz::PlayerState;

namespace {

// A sink that says whether it still holds a device-lost callback.
struct RecordingSink : spz::fake::AudioSink {
    bool holds_callback = false;
    void on_device_lost(std::function<void()> fn) override {
        holds_callback = static_cast<bool>(fn);
        spz::fake::AudioSink::on_device_lost(std::move(fn));
    }
};

// An engine whose load() waits, on the thread that calls it, until the test lets it go.
struct HeldEngine : spz::fake::Engine {
    std::mutex mutex;
    std::condition_variable wake;
    bool inside = false, released = false;
    bool load(const std::vector<std::uint8_t>& tune, int subtune, const spz::EngineSettings& settings,
              std::string* why) override {
        {
            std::unique_lock<std::mutex> lock(mutex);
            inside = true;
            wake.notify_all();
            wake.wait(lock, [this] { return released; });
        }
        return spz::fake::Engine::load(tune, subtune, settings, why);
    }
    void wait_inside() {
        std::unique_lock<std::mutex> lock(mutex);
        wake.wait(lock, [this] { return inside; });
    }
    void release() {
        std::lock_guard<std::mutex> lock(mutex);
        released = true;
        wake.notify_all();
    }
};

// Asks of a backend on other threads, each to be answered within half a second. The answers are waited
// for only in done(), once whatever held them has been let go, so that a test that fails does not hang.
struct Asks {
    std::vector<std::future<void>> asked;
    template <typename Fn>
    bool at_once(Fn fn) {
        asked.push_back(std::async(std::launch::async, fn));
        return asked.back().wait_for(std::chrono::milliseconds(500)) == std::future_status::ready;
    }
    void done() {
        for (std::future<void>& one : asked) one.wait();
    }
};

// The whole sound path, with a worker of its own for the device (SPEC-078's attempts).
struct DevicePath : PlayerFixture {
    spz::fake::Engine engine;
    spz::fake::AudioSink sink;
    spz::fake::Executor device_worker;
    spz::SoftwareBackend software{&engine, &sink, &clock, &ui, &device_worker};

    DevicePath() {
        sound = &software;
        make();
    }
    void play_for(int ms) {
        for (int t = 0; t < ms; t += 5) {
            while (software.render_step()) {
            }
            clock.advance(5);
            sink.pull(240);
            software.render_step();
            while (worker.run_one() || ui.run_one() || device_worker.run_one()) {
            }
        }
    }
};

}  // namespace

TEST_CASE("SPEC-078 SPEC-022 once the software backend has gone, the sink holds no device-lost callback of it") {
    spz::fake::Clock clock;
    spz::fake::Executor ui;
    spz::fake::Engine engine;
    RecordingSink sink;
    {
        spz::SoftwareBackend backend(&engine, &sink, &clock, &ui);
        CHECK(sink.holds_callback);
    }
    CHECK_FALSE(sink.holds_callback);
    sink.drop_device();                       // nothing of the backend is called
    CHECK(ui.waiting() == 0);
}

TEST_CASE("SPEC-022 SPEC-070 while the engine loads a tune, the backend's other operations and the render loop answer at once") {
    spz::fake::Clock clock;
    spz::fake::Executor ui;
    HeldEngine engine;
    spz::fake::AudioSink sink;
    spz::SoftwareBackend backend(&engine, &sink, &clock, &ui);
    const std::vector<std::uint8_t> tune(200, 0);
    spz::EngineSettings settings;
    settings.sample_rate = 48000;
    bool loaded = false;
    std::thread loader([&] { loaded = backend.load(tune, 1, settings, nullptr); });
    engine.wait_inside();

    std::uint8_t registers[spz::Engine::kRegisters];
    bool got_registers = true, rendered = true;
    Asks asks;
    const bool registers_at_once = asks.at_once([&] { got_registers = backend.registers(0, registers); });
    const bool chips_at_once = asks.at_once([&] { backend.chips(); });
    const bool mute_at_once = asks.at_once([&] { backend.set_muted(0, 0, true); });
    const bool render_at_once = asks.at_once([&] { rendered = backend.render_step(); });
    engine.release();
    asks.done();
    loader.join();
    CHECK(registers_at_once);
    CHECK(chips_at_once);
    CHECK(mute_at_once);
    CHECK(render_at_once);
    CHECK_FALSE(got_registers);               // nothing is loaded yet while it loads
    CHECK_FALSE(rendered);
    CHECK(loaded);
    CHECK(backend.registers(0, registers));
}

TEST_CASE("SPEC-022 SPEC-070 a stop while the engine loads wins: that load fails and nothing stays loaded") {
    spz::fake::Clock clock;
    spz::fake::Executor ui;
    HeldEngine engine;
    spz::fake::AudioSink sink;
    spz::SoftwareBackend backend(&engine, &sink, &clock, &ui);
    const std::vector<std::uint8_t> tune(200, 0);
    spz::EngineSettings settings;
    settings.sample_rate = 48000;
    bool loaded = true;
    std::thread loader([&] { loaded = backend.load(tune, 1, settings, nullptr); });
    engine.wait_inside();
    Asks asks;
    const bool stop_at_once = asks.at_once([&] { backend.stop(); });
    engine.release();
    asks.done();
    loader.join();
    CHECK(stop_at_once);
    CHECK_FALSE(loaded);
    std::uint8_t registers[spz::Engine::kRegisters];
    CHECK_FALSE(backend.registers(0, registers));
    CHECK_FALSE(backend.render_step());
}

TEST_CASE("SPEC-078 SPEC-022 with a worker, the backend opens a lost or gone device again on that worker, never on the thread that asks") {
    DevicePath p;
    p.play({kLong}, 0);
    p.play_for(1000);
    p.sink.refuse = true;
    const int opens = p.sink.opens;
    p.sink.drop_device();
    p.ui.run_all();                                         // the loss is handled on the UI thread
    p.clock.advance(spz::SoftwareBackend::kReopenEveryMs);  // the first attempt is due
    p.ui.run_all();
    CHECK(p.sink.opens == opens);                           // not opened there
    CHECK(p.device_worker.waiting() == 1);                  // but on the worker
    p.play_for(1500);                                       // five attempts have failed by now
    CHECK(p.sink.opens == opens + spz::SoftwareBackend::kReopenTries);
    CHECK(p.state() == PlayerState::Paused);
    CHECK(p.notices.ids == Calls{"MSG_NO_SOUND"});

    p.sink.refuse = false;
    p.player->play_pause();                                 // resume(): the device is tried again
    CHECK(p.sink.opens == opens + spz::SoftwareBackend::kReopenTries);
    CHECK(p.device_worker.waiting() == 1);
    p.play_for(1000);
    CHECK(p.sink.opens == opens + spz::SoftwareBackend::kReopenTries + 1);
    CHECK(p.sink.is_open);
    CHECK(p.state() == PlayerState::Playing);
    CHECK_FALSE(p.sink.received.empty());
}

TEST_CASE("SPEC-025 SPEC-040 a player another has taken the backend from leaves it alone: its late load does not stop the new player's tune") {
    PlayerFixture f;
    // The first player's load reaches the backend, and its answer waits for the UI thread.
    f.player->play(f.entries({kOne}), 0);
    REQUIRE(f.worker.run_one());
    // A new root: the first player is stopped and kept, and a new one is made on the same backend.
    std::unique_ptr<spz::Player> old = std::move(f.player);
    old->stop();
    f.make();
    const std::size_t mark = f.mark();
    f.player->play(f.entries({kThree}), 0);
    f.settle();                                             // the new load, then both answers in their order
    CHECK(f.state() == PlayerState::Playing);
    CHECK(f.calls_since(mark) == Calls{"load 2", "start"});
    CHECK(f.backend.loaded);
    CHECK(f.backend.running);
    CHECK(old->status().state == PlayerState::Idle);
}
