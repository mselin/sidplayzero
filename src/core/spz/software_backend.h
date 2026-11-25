// SPDX-License-Identifier: GPL-3.0-or-later
// The software backend (SPEC-025, SPEC-070): a SoundBackend made of an Engine, a ring buffer and an
// AudioSink. The render loop asks the engine for samples and fills the buffer; the sink's callback
// empties it. Only the one place that creates it, and its own tests, name this type: the player and
// everything else know a SoundBackend.
//
// Three threads meet here. The operations of SoundBackend are called by the player. render_step() is the
// render loop's body: on the device the render thread calls it again and again; in the tests the test
// calls it. The sink's callback runs on the audio thread and must not lock, allocate or wait, so it only
// copies from the ring buffer and counts. Events reach the listener as jobs posted to the UI thread.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "spz/audio_sink.h"
#include "spz/clock.h"
#include "spz/engine.h"
#include "spz/executor.h"
#include "spz/sound_backend.h"
#include "spz/types.h"

namespace spz {

class SoftwareBackend : public SoundBackend {
public:
    static constexpr std::size_t kRingFrames = 8192;    // the ring buffer, in stereo frames (SPEC-070)
    static constexpr std::size_t kStartFrames = 2048;   // output for a subtune starts when this many are in it
    static constexpr int kChunkMs = 5;                  // the engine is asked for about this much at a time
    static constexpr int kSkipTimes = 8;                // skipping: this many chunks rendered for one played (SPEC-066)
    static constexpr Millis kQuietAfterSkipMs = 1000;   // no underruns are counted for this long after skipping
    static constexpr int kReopenTries = 5;              // SPEC-078: how often a lost device is opened again,
    static constexpr Millis kReopenEveryMs = 200;       // and how far apart

    // `ui` is the UI thread's executor: every event for the listener is posted to it. Timers set on
    // `clock` fire on the UI thread too (SPEC-078's attempts to open the device again are such timers).
    SoftwareBackend(Engine* engine, AudioSink* sink, Clock* clock, Executor* ui);
    ~SoftwareBackend() override;

    void set_listener(SoundBackendListener* listener) override;

    // Loads the subtune into the engine and opens the sink at settings.sample_rate if no stream is open
    // at that rate (SPEC-073). What is in the buffer is thrown away. Fails with "MSG_NO_SOUND" if the
    // sink cannot be opened (SPEC-078), and with the engine's reason if the engine refuses the tune.
    bool load(const std::vector<std::uint8_t>& tune, int subtune, const EngineSettings& settings,
              std::string* why) override;
    void start() override;      // the sink gets silence until kStartFrames are buffered, then the first sample
    void pause() override;      // the sink gets silence; the buffer keeps its contents
    // Carries on with the sample after the last one played. After `device_gone` it first opens the sink
    // again (SPEC-078).
    void resume() override;
    void stop() override;       // unloads; the buffer is thrown away and the sink is silent from its next callback

    // After exactly this many samples of the subtune no more are rendered; when the sink has played the
    // last of them, `song_ended` is reported (SPEC-064). 0 means never.
    void set_length(std::uint64_t samples) override;
    // The place in the subtune of the next sample the sink will be given: the samples played so far,
    // and, while skipping, those passed over as well. 0 after load().
    std::uint64_t position() const override;

    // Chips and voices count from 0. A change takes effect within one render chunk (SPEC-074).
    void set_muted(int chip, int voice, bool muted) override;
    void set_filter(int chip, bool on) override;
    void set_skip(bool on) override;
    bool registers(int chip, std::uint8_t out[Engine::kRegisters]) override;
    int chips() const override;
    BackendAbilities can() const override;      // all four
    // Frames of silence the sink was given because the buffer had too little: counted only while
    // playing, after output for the subtune has started, before its last sample, and not while skipping
    // or within a second after (SPEC-070, SPEC-066). Counted over the backend's whole life.
    std::uint64_t underruns() const override;

    // One turn of the render loop (SPEC-070). It asks the engine once for about 5 ms of samples, never
    // more than the buffer has room for or the subtune has left, and writes what the engine gives into
    // the ring buffer; while skipping it renders up to kSkipTimes chunks and keeps one of them.
    // It is also where the backend notices what the audio callback has done: once the callback has
    // played the subtune's last sample, the next turn posts `song_ended`.
    // Returns true if it rendered anything. False means the loop may wait, because there is nothing to
    // render now: nothing is loaded, the buffer is full, the subtune has been rendered to its end, or
    // the device is lost.
    bool render_step();

    // Frames in the ring buffer now.
    std::size_t buffered() const;

private:
    Engine* engine_ = nullptr;
    AudioSink* sink_ = nullptr;
    Clock* clock_ = nullptr;
    Executor* ui_ = nullptr;
};

}  // namespace spz
