// SPDX-License-Identifier: GPL-3.0-or-later
// SPEC-207 (part `screen-off`): the backlight goes off while a tune plays and no key is pressed, and is
// always put back.
#include "spz/screen_off.h"

namespace spz {

namespace {

// Puts back the brightness that state.ini holds, if it holds one, and clears it (FIND-024).
void put_back(Backlight* backlight, StateFile* state) {
    const int saved = state->state().backlight_saved;
    if (saved >= 0) backlight->set(saved);
    state->set_backlight_saved(-1);
}

}  // namespace

ScreenOff::ScreenOff(Clock* clock, Executor* quick, Backlight* backlight, StateFile* state)
    : clock_(clock), quick_(quick), backlight_(backlight), state_(state) {}

ScreenOff::~ScreenOff() {
    if (timer_set_) clock_->cancel(timer_);
}

void ScreenOff::at_start() {
    available_ = backlight_->can_change();
    if (state_->state().backlight_saved < 0) return;
    // A run was killed with the screen off (SPEC-202).
    if (available_) {
        put_back(backlight_, state_);
    } else {
        state_->set_backlight_saved(-1);
    }
}

bool ScreenOff::available() const { return available_; }

void ScreenOff::set_after_s(int seconds) {
    after_s_ = seconds > 0 ? seconds : 0;
    rearm();
}

void ScreenOff::set_playing(bool playing) {
    if (playing == playing_) return;   // one tune after another plays on: the seconds go on counting
    playing_ = playing;
    if (!playing && off_) {
        off_ = false;
        Backlight* backlight = backlight_;
        StateFile* state = state_;
        quick_->post([backlight, state] { put_back(backlight, state); });
    }
    rearm();
}

bool ScreenOff::key() {
    if (off_) {
        // The key turns the screen on and does nothing else.
        off_ = false;
        Backlight* backlight = backlight_;
        StateFile* state = state_;
        quick_->post([backlight, state] { put_back(backlight, state); });
        rearm();
        return true;
    }
    rearm();   // the seconds count from the last key
    return false;
}

void ScreenOff::at_exit() {
    if (timer_set_) clock_->cancel(timer_);
    timer_set_ = false;
    if (!off_) return;
    off_ = false;
    put_back(backlight_, state_);
}

bool ScreenOff::is_off() const { return off_; }

void ScreenOff::rearm() {
    if (timer_set_) clock_->cancel(timer_);
    timer_set_ = false;
    if (!available_ || !playing_ || after_s_ <= 0 || off_) return;
    timer_set_ = true;
    timer_ = clock_->call_after(static_cast<std::int64_t>(after_s_) * 1000, [this] {
        timer_set_ = false;
        off_ = true;
        // The timer fires on the UI thread, which touches neither the card nor the backlight (SPEC-022).
        Backlight* backlight = backlight_;
        StateFile* state = state_;
        quick_->post([backlight, state] {
            int percent = 0;
            if (!backlight->get(&percent)) return;
            // The brightness to put back is on the card before the backlight goes off.
            state->set_backlight_saved(percent);
            if (state->save_now() != FileStatus::Ok) return;
            backlight->set(0);
        });
    });
}

}  // namespace spz
