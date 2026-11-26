// SPDX-License-Identifier: GPL-3.0-or-later
// The software backend, made and run (SPEC-025, SPEC-070, SPEC-022): this is the one place that creates
// `core`'s software backend. It makes the backend from an Engine, an AudioSink, the clock and the UI
// thread's executor, gives the player its SoundBackend, and runs its render loop on the render thread.
// Everything else knows a SoundBackend only.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>

#include "spz/audio_sink.h"
#include "spz/clock.h"
#include "spz/engine.h"
#include "spz/executor.h"
#include "spz/sound_backend.h"

namespace spz::platform {

class SoftwareSound {
public:
    // `worker`, if given, is where the device is opened again after it was lost (SoftwareBackend).
    SoftwareSound(Engine* engine, AudioSink* sink, Clock* clock, Executor* ui, Executor* worker = nullptr);
    // Stops the render thread first, if it runs.
    ~SoftwareSound();
    SoftwareSound(const SoftwareSound&) = delete;
    SoftwareSound& operator=(const SoftwareSound&) = delete;

    // What the player plays through.
    SoundBackend* backend();

    // One turn of the render loop (SPEC-070), for a test or a tool that turns it itself: true if it
    // rendered anything.
    bool render_step();
    // Frames in the ring buffer now.
    std::size_t buffered() const;
    // The time spent in turns of the render loop that rendered something, in nanoseconds of the real clock,
    // over the backend's whole life: what the self-test's render load is made of (SPEC-241). Any thread.
    std::int64_t render_ns() const { return render_ns_.load(); }

    // The render thread (SPEC-022): turns the render loop again and again, and waits a millisecond
    // whenever there is nothing to render. Never touches LVGL or files.
    void start_render_thread();
    void stop_render_thread();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::atomic<bool> running_{false};
    std::atomic<std::int64_t> render_ns_{0};
    bool timed_step();
    std::thread thread_;
};

}  // namespace spz::platform
