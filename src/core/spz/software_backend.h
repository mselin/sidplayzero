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

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
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
    // The audio callback (SPEC-070): copies from the ring buffer, fills the rest with silence, and counts.
    // It takes no lock, allocates nothing and does not wait.
    void pull(std::int16_t* out, std::size_t frames);
    // Stops the callback from touching the buffer, and waits until a callback under way has returned.
    void quiet_callback();
    // Lets the callback play again if everything says it may.
    void update_active();
    // Opens the stream at rate_. False if no device opens.
    bool open_stream();
    void close_stream();
    // Throws away what is in the buffer. The callback must be quiet.
    void clear_buffer();
    std::uint64_t left_to_render() const;      // frames of the subtune not rendered yet
    void apply_controls();                      // mutes and filters, to the engine, on the render thread
    // SPEC-078: the device was lost; and the attempts to open it again, on the UI thread.
    void device_was_lost();
    void try_reopen();
    // Gives an event to the listener as a job on the UI thread.
    void post(void (SoundBackendListener::*event)());

    Engine* engine_ = nullptr;
    AudioSink* sink_ = nullptr;
    Clock* clock_ = nullptr;
    Executor* ui_ = nullptr;
    SoundBackendListener* listener_ = nullptr;
    std::shared_ptr<bool> alive_;               // jobs and timers posted by this backend look here first

    // What the player's thread and the render thread share, under mutex_.
    mutable std::mutex mutex_;
    bool loaded_ = false, started_ = false, paused_ = false, skip_ = false;
    unsigned rate_ = 48000;
    std::uint64_t length_ = 0;                  // 0: never ends
    std::uint64_t rendered_ = 0;                // frames of the subtune the engine has produced, skipped ones too
    bool end_told_ = false;
    std::int64_t skip_off_ms_ = -1;             // when skipping last ended; -1 if it never has
    bool muted_[3][3] = {};
    bool filter_on_[3] = {true, true, true};
    bool controls_changed_ = false;
    int chips_ = 1;
    std::vector<std::int16_t> chunk_;           // what the engine renders into

    // The stream, on the player's thread.
    bool open_ = false;
    unsigned open_rate_ = 0;
    bool gone_ = false;
    int tries_ = 0;
    Clock::TimerId reopen_timer_ = 0;
    bool reopening_ = false;

    // What the audio callback reads and writes.
    std::vector<std::int16_t> ring_;
    std::atomic<std::uint64_t> written_{0};     // frames written into the ring, ever
    std::atomic<std::uint64_t> read_{0};        // frames taken from it, ever
    std::atomic<bool> active_{false};           // loaded, started, not paused, the stream open and not lost
    std::atomic<bool> busy_{false};             // a callback is under way
    std::atomic<bool> output_started_{false};   // the subtune's first frame has been given to the sink
    std::atomic<bool> all_rendered_{false};     // the subtune's last frame is in the ring
    std::atomic<std::uint64_t> end_at_{0};      // with all_rendered_: written_ after the subtune's last frame
    std::atomic<std::uint64_t> played_{0};      // frames of the subtune given to the sink
    std::atomic<std::uint64_t> skipped_{0};     // frames of the subtune passed over while skipping
    std::atomic<std::uint64_t> underruns_{0};
    std::atomic<bool> counting_{true};          // underruns are counted now (not while skipping or just after)
    std::atomic<bool> lost_{false};             // the device is lost: rendering waits
};

}  // namespace spz
