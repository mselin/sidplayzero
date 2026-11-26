// SPDX-License-Identifier: GPL-3.0-or-later
// The software backend (SPEC-025, SPEC-070): an Engine, a ring buffer and an AudioSink.
//
// Who does what: the player's operations come from one thread (the UI thread, or the quick worker for
// load()); render_step() from the render thread; pull() from the audio thread. The first two share the
// state under mutex_. The audio callback shares only the ring and atomics with them: the ring is a
// single-producer, single-consumer queue (render_step() writes, pull() reads), and whoever throws its
// contents away first makes the callback quiet (quiet_callback()), so that nothing else ever moves read_.
#include "spz/software_backend.h"

#include <algorithm>
#include <cstring>
#include <thread>

namespace spz {

SoftwareBackend::SoftwareBackend(Engine* engine, AudioSink* sink, Clock* clock, Executor* ui)
    : engine_(engine), sink_(sink), clock_(clock), ui_(ui), alive_(std::make_shared<bool>(true)),
      ring_(2 * kRingFrames, 0) {
    std::weak_ptr<bool> alive = alive_;
    // The device can be lost on any thread: the callback only says so, and the rest is done on the UI thread.
    sink_->on_device_lost([this, alive] {
        const std::shared_ptr<bool> still = alive.lock();
        if (!still || !*still) return;
        lost_.store(true);
        active_.store(false);
        ui_->post([this, alive] {
            const std::shared_ptr<bool> here = alive.lock();
            if (here && *here) device_was_lost();
        });
    });
}

SoftwareBackend::~SoftwareBackend() {
    *alive_ = false;
    if (reopen_timer_ != 0) clock_->cancel(reopen_timer_);
    quiet_callback();
    if (open_) sink_->close();
}

void SoftwareBackend::set_listener(SoundBackendListener* listener) { listener_ = listener; }

// ---- The audio callback --------------------------------------------------------------------------------------

void SoftwareBackend::pull(std::int16_t* out, std::size_t frames) {
    busy_.store(true);
    if (!active_.load()) {
        std::memset(out, 0, frames * 2 * sizeof(std::int16_t));
        busy_.store(false);
        return;
    }
    const std::uint64_t written = written_.load(std::memory_order_acquire);
    const std::uint64_t read = read_.load(std::memory_order_relaxed);
    const std::uint64_t buffered = written - read;
    if (!output_started_.load()) {
        if (buffered >= kStartFrames || (all_rendered_.load() && buffered > 0)) {
            output_started_.store(true);
        } else {
            std::memset(out, 0, frames * 2 * sizeof(std::int16_t));
            busy_.store(false);
            return;
        }
    }
    const std::size_t taken = static_cast<std::size_t>(std::min<std::uint64_t>(frames, buffered));
    std::size_t at = static_cast<std::size_t>(read % kRingFrames);
    std::size_t done = 0;
    while (done < taken) {
        const std::size_t run = std::min(taken - done, kRingFrames - at);
        std::memcpy(out + 2 * done, ring_.data() + 2 * at, run * 2 * sizeof(std::int16_t));
        done += run;
        at = (at + run) % kRingFrames;
    }
    read_.store(read + taken, std::memory_order_release);
    played_.fetch_add(taken);
    if (taken < frames) {
        std::memset(out + 2 * taken, 0, (frames - taken) * 2 * sizeof(std::int16_t));
        const bool past_the_end = all_rendered_.load() && read + taken >= end_at_.load();
        if (counting_.load() && !past_the_end) underruns_.fetch_add(frames - taken);
    }
    busy_.store(false);
}

void SoftwareBackend::quiet_callback() {
    active_.store(false);
    while (busy_.load()) std::this_thread::yield();
}

void SoftwareBackend::update_active() {
    active_.store(loaded_ && started_ && !paused_ && open_ && !lost_.load());
}

// ---- The stream ------------------------------------------------------------------------------------------------

bool SoftwareBackend::open_stream() {
    if (reopen_timer_ != 0) {
        clock_->cancel(reopen_timer_);
        reopen_timer_ = 0;
    }
    reopening_ = false;
    if (!sink_->open(rate_, [this](std::int16_t* out, std::size_t frames) { pull(out, frames); })) {
        open_ = false;
        return false;
    }
    open_ = true;
    open_rate_ = rate_;
    gone_ = false;
    lost_.store(false);
    return true;
}

void SoftwareBackend::close_stream() {
    quiet_callback();
    if (open_) sink_->close();
    open_ = false;
}

void SoftwareBackend::post(void (SoundBackendListener::*event)()) {
    std::weak_ptr<bool> alive = alive_;
    ui_->post([this, alive, event] {
        const std::shared_ptr<bool> here = alive.lock();
        if (here && *here && listener_) (listener_->*event)();
    });
}

void SoftwareBackend::device_was_lost() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (reopening_ || !open_) return;
    post(&SoundBackendListener::device_lost);
    close_stream();
    lost_.store(true);
    reopening_ = true;
    tries_ = 0;
    std::weak_ptr<bool> alive = alive_;
    reopen_timer_ = clock_->call_after(kReopenEveryMs, [this, alive] {
        const std::shared_ptr<bool> here = alive.lock();
        if (here && *here) try_reopen();
    });
}

void SoftwareBackend::try_reopen() {
    std::lock_guard<std::mutex> lock(mutex_);
    reopen_timer_ = 0;
    if (!reopening_) return;
    ++tries_;
    if (open_stream()) {
        update_active();
        post(&SoundBackendListener::device_back);
        return;
    }
    if (tries_ < kReopenTries) {
        reopening_ = true;
        std::weak_ptr<bool> alive = alive_;
        reopen_timer_ = clock_->call_after(kReopenEveryMs, [this, alive] {
            const std::shared_ptr<bool> here = alive.lock();
            if (here && *here) try_reopen();
        });
        return;
    }
    // It did not come back: the player pauses, and Play/Pause tries again (resume()).
    reopening_ = false;
    gone_ = true;
    lost_.store(true);
    post(&SoundBackendListener::device_gone);
}

// ---- The operations ---------------------------------------------------------------------------------------------

void SoftwareBackend::clear_buffer() {
    written_.store(0);
    read_.store(0);
    played_.store(0);
    skipped_.store(0);
    output_started_.store(false);
    all_rendered_.store(false);
    end_at_.store(0);
}

bool SoftwareBackend::load(const std::vector<std::uint8_t>& tune, int subtune, const EngineSettings& settings,
                           std::string* why) {
    std::lock_guard<std::mutex> lock(mutex_);
    quiet_callback();
    loaded_ = false;
    started_ = false;
    paused_ = false;
    clear_buffer();
    std::string reason;
    if (!engine_->load(tune, subtune, settings, &reason)) {
        if (why) *why = reason.empty() ? "MSG_ENGINE_REFUSED" : reason;
        return false;
    }
    rate_ = settings.sample_rate;
    if (!open_ || open_rate_ != rate_ || gone_) {
        if (open_) close_stream();
        if (!open_stream()) {
            if (why) *why = "MSG_NO_SOUND";
            return false;
        }
    }
    loaded_ = true;
    length_ = 0;
    rendered_ = 0;
    end_told_ = false;
    chips_ = engine_->chips();
    // A new subtune starts with everything on; the player says again what it wants off.
    for (int chip = 0; chip < 3; ++chip) {
        filter_on_[chip] = true;
        for (int voice = 0; voice < 3; ++voice) muted_[chip][voice] = false;
    }
    controls_changed_ = true;
    const std::size_t chunk = std::max<std::size_t>(1, static_cast<std::size_t>(rate_) * kChunkMs / 1000);
    chunk_.assign(2 * chunk, 0);
    return true;
}

void SoftwareBackend::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    started_ = true;
    paused_ = false;
    update_active();
}

void SoftwareBackend::pause() {
    std::lock_guard<std::mutex> lock(mutex_);
    paused_ = true;
    quiet_callback();
}

void SoftwareBackend::resume() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (loaded_ && (gone_ || !open_) && !reopening_) {
        if (!open_stream()) {
            lost_.store(true);
            gone_ = true;
            std::weak_ptr<bool> alive = alive_;
            ui_->post([this, alive] {
                const std::shared_ptr<bool> here = alive.lock();
                if (here && *here && listener_) listener_->failed("MSG_NO_SOUND");
            });
        }
    }
    paused_ = false;
    update_active();
}

void SoftwareBackend::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    quiet_callback();
    loaded_ = false;
    started_ = false;
    paused_ = false;
    length_ = 0;
    rendered_ = 0;
    clear_buffer();
}

void SoftwareBackend::set_length(std::uint64_t samples) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool was_active = active_.load();
    quiet_callback();
    length_ = samples;
    end_told_ = false;
    all_rendered_.store(false);
    if (samples > 0 && rendered_ >= samples) {
        // Rendered past the end already: what lies past it in the buffer is taken back.
        const std::uint64_t excess = rendered_ - samples;
        const std::uint64_t written = written_.load();
        const std::uint64_t unread = written - read_.load();
        const std::uint64_t back = std::min(excess, unread);
        written_.store(written - back);
        rendered_ = samples;
        end_at_.store(written - back);
        all_rendered_.store(true);
    }
    if (was_active) update_active();
}

std::uint64_t SoftwareBackend::position() const { return played_.load() + skipped_.load(); }

void SoftwareBackend::set_muted(int chip, int voice, bool muted) {
    if (chip < 0 || chip > 2 || voice < 0 || voice > 2) return;
    std::lock_guard<std::mutex> lock(mutex_);
    muted_[chip][voice] = muted;
    controls_changed_ = true;
}

void SoftwareBackend::set_filter(int chip, bool on) {
    if (chip < 0 || chip > 2) return;
    std::lock_guard<std::mutex> lock(mutex_);
    filter_on_[chip] = on;
    controls_changed_ = true;
}

void SoftwareBackend::set_skip(bool on) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (skip_ && !on) skip_off_ms_ = clock_->now_ms();
    skip_ = on;
    counting_.store(false);
}

bool SoftwareBackend::registers(int chip, std::uint8_t out[Engine::kRegisters]) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!loaded_ || chip < 0 || chip >= chips_) return false;
    return engine_->registers(chip, out);
}

int SoftwareBackend::chips() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return loaded_ ? chips_ : engine_->chips();
}

BackendAbilities SoftwareBackend::can() const { return BackendAbilities{true, true, true, true}; }

std::uint64_t SoftwareBackend::underruns() const { return underruns_.load(); }

std::size_t SoftwareBackend::buffered() const {
    return static_cast<std::size_t>(written_.load() - read_.load());
}

// ---- The render loop ---------------------------------------------------------------------------------------------

std::uint64_t SoftwareBackend::left_to_render() const {
    if (length_ == 0) return UINT64_MAX;
    return length_ > rendered_ ? length_ - rendered_ : 0;
}

void SoftwareBackend::apply_controls() {
    if (!controls_changed_) return;
    controls_changed_ = false;
    for (int chip = 0; chip < chips_ && chip < 3; ++chip) {
        for (int voice = 0; voice < 3; ++voice) engine_->set_muted(chip, voice, muted_[chip][voice]);
        engine_->set_filter(chip, filter_on_[chip]);
    }
}

bool SoftwareBackend::render_step() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!loaded_ || lost_.load()) return false;
    apply_controls();
    // Underruns are not counted while skipping, nor for a second after (SPEC-066).
    counting_.store(!skip_ && (skip_off_ms_ < 0 || clock_->now_ms() - skip_off_ms_ >= kQuietAfterSkipMs));

    if (all_rendered_.load()) {
        if (!end_told_ && read_.load() >= end_at_.load()) {
            end_told_ = true;
            post(&SoundBackendListener::song_ended);
        }
        return false;
    }
    const std::uint64_t room = kRingFrames - (written_.load() - read_.load());
    if (room == 0) return false;
    const std::size_t chunk = chunk_.size() / 2;
    if (chunk == 0) return false;

    if (skip_) {
        // Rendered and passed over: kSkipTimes - 1 chunks for every one that is played.
        for (int i = 0; i + 1 < kSkipTimes; ++i) {
            const std::size_t asked = static_cast<std::size_t>(std::min<std::uint64_t>(chunk, left_to_render()));
            if (asked == 0) break;
            const std::size_t got = engine_->render(chunk_.data(), asked);
            if (got == 0) break;
            rendered_ += got;
            skipped_.fetch_add(got);
        }
    }

    const std::size_t asked =
        static_cast<std::size_t>(std::min<std::uint64_t>(std::min<std::uint64_t>(chunk, room), left_to_render()));
    std::size_t got = 0;
    if (asked > 0) got = std::min(engine_->render(chunk_.data(), asked), asked);
    if (got > 0) {
        const std::uint64_t written = written_.load();
        std::size_t at = static_cast<std::size_t>(written % kRingFrames);
        std::size_t done = 0;
        while (done < got) {
            const std::size_t run = std::min(got - done, kRingFrames - at);
            std::memcpy(ring_.data() + 2 * at, chunk_.data() + 2 * done, run * 2 * sizeof(std::int16_t));
            done += run;
            at = (at + run) % kRingFrames;
        }
        rendered_ += got;
        written_.store(written + got, std::memory_order_release);
    }
    if (length_ > 0 && rendered_ >= length_) {
        end_at_.store(written_.load());
        all_rendered_.store(true);
    }
    return got > 0;
}

}  // namespace spz
