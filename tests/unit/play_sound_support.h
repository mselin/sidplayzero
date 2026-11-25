// SPDX-License-Identifier: GPL-3.0-or-later
// What the tests of the software backend share: the backend set up against the fake Engine, AudioSink,
// Clock and Executor, with the render loop as a function the test calls (SPEC-070), and ways to say what
// the sink should have received.
//
// The fake engine's sound is a ramp that a test can predict: the left sample of frame i of subtune s is
// (s x 1000 + i) modulo 32000, and the right one its negative. So every frame says where in which subtune
// it came from, and a frame of silence is 0, 0.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "spz/software_backend.h"
#include "support.h"

namespace spz::test {

using Samples = std::vector<std::int16_t>;      // left samples, one for each frame

// The left samples of frames [first, first + count) of a subtune, as the fake engine renders them.
inline Samples ramp(int subtune, std::uint64_t first, std::uint64_t count) {
    Samples samples;
    samples.reserve(count);
    for (std::uint64_t i = 0; i < count; ++i) samples.push_back(fake::Engine::sample(subtune, first + i));
    return samples;
}

inline Samples operator+(Samples a, const Samples& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

// A list of samples without the zeros: for comparing what was heard when silence may lie between the
// songs. (A ramp passes through zero once in 32,000 frames, so the same is done to what is expected.)
inline Samples without_zeros(const Samples& samples) {
    Samples kept;
    for (const std::int16_t sample : samples) {
        if (sample != 0) kept.push_back(sample);
    }
    return kept;
}

// "" if two lists of samples are equal; otherwise where they first differ, for a failed check to show.
inline std::string difference(const Samples& found, const Samples& expected) {
    const std::size_t shorter = found.size() < expected.size() ? found.size() : expected.size();
    for (std::size_t i = 0; i < shorter; ++i) {
        if (found[i] != expected[i]) {
            return "frame " + std::to_string(i) + " is " + std::to_string(found[i]) + ", not " + std::to_string(expected[i]);
        }
    }
    if (found.size() != expected.size()) {
        return std::to_string(found.size()) + " frames, not " + std::to_string(expected.size());
    }
    return "";
}

// "" if `found` is how `expected` begins: nothing in it out of place, repeated or left out, though it may
// stop early. Otherwise where it first differs.
inline std::string difference_from_start(const Samples& found, const Samples& expected) {
    if (found.size() > expected.size()) {
        return std::to_string(found.size()) + " frames, more than the " + std::to_string(expected.size()) + " expected";
    }
    return difference(found, Samples(expected.begin(), expected.begin() + static_cast<std::ptrdiff_t>(found.size())));
}

// What a sink received: the left samples of frames [from, from + count), or of all frames from `from`.
inline Samples received(const fake::AudioSink& sink, std::size_t from = 0, std::size_t count = static_cast<std::size_t>(-1)) {
    Samples samples;
    const std::size_t frames = sink.received.size() / 2;
    for (std::size_t i = from; i < frames && i - from < count; ++i) samples.push_back(sink.received[2 * i]);
    return samples;
}

// How many frames a sink has received.
inline std::size_t frames(const fake::AudioSink& sink) { return sink.received.size() / 2; }

// True if every frame's right sample is the negative of its left one: what was given to the sink is the
// engine's stereo frames, whole.
inline bool stereo_whole(const fake::AudioSink& sink) {
    for (std::size_t i = 0; i + 1 < sink.received.size(); i += 2) {
        if (sink.received[i + 1] != static_cast<std::int16_t>(-sink.received[i])) return false;
    }
    return true;
}

// A listener that keeps the backend's events in the order they came.
struct Events : SoundBackendListener {
    std::vector<std::string> log;

    void song_ended() override { log.push_back("song_ended"); }
    void device_lost() override { log.push_back("device_lost"); }
    void device_back() override { log.push_back("device_back"); }
    void device_gone() override { log.push_back("device_gone"); }
    void failed(const std::string& reason) override { log.push_back("failed " + reason); }
    int count(const std::string& what) const {
        int found = 0;
        for (const std::string& event : log) found += event == what ? 1 : 0;
        return found;
    }
};

// The software backend against the fakes.
struct Sound {
    static constexpr std::size_t kTick = 240;       // the frames the device takes in 5 ms at 48,000 Hz

    fake::Engine engine;
    fake::AudioSink sink;
    fake::Clock clock;
    fake::Executor ui;
    Events events;
    SoftwareBackend backend{&engine, &sink, &clock, &ui};
    EngineSettings settings;                        // 48,000 Hz unless a test says otherwise
    Bytes tune = fixture("tunes/commando_test.sid");    // the fake engine does not look at it

    Sound() { backend.set_listener(&events); }

    // Loads a subtune and gives its length in samples (0: never ends). False if the load failed.
    bool load(int subtune = 1, std::uint64_t length = 0) {
        why.clear();
        if (!backend.load(tune, subtune, settings, &why)) return false;
        backend.set_length(length);
        return true;
    }
    std::string why;                                // the reason of the last load that failed

    // The render loop runs until it would wait.
    void fill() {
        int turns = 0;
        while (backend.render_step()) REQUIRE(++turns < 1000000);
    }
    // 5 ms with an engine that keeps up: the render loop fills the buffer, the clock moves on, the device
    // takes its frames, the render loop sees what was taken, and the UI thread gets what was posted.
    void tick() {
        fill();
        clock.advance(5);
        sink.pull(kTick);
        backend.render_step();
        ui.run_all();
    }
    void play_for(int ms) {
        for (int t = 0; t < ms; t += 5) tick();
    }
    // Loaded, started, and the buffer filled: the next frame the sink takes is the subtune's first.
    void ready(int subtune = 1, std::uint64_t length = 0) {
        REQUIRE(load(subtune, length));
        backend.start();
        fill();
        REQUIRE(backend.buffered() >= SoftwareBackend::kStartFrames);
    }
};

}  // namespace spz::test
