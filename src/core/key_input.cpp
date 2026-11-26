// SPDX-License-Identifier: GPL-3.0-or-later
// The one place where a key becomes an action (SPEC-111); the rules are in spz/key_input.h.
#include "spz/key_input.h"

#include <utility>

#include "spz/keymap.h"
#include "spz/keys.h"

namespace spz {

namespace {

const char* const kRepeating[] = {"up",           "down",          "page_up",      "page_down",
                                  "cursor_left",  "cursor_right",  "delete_before", "delete_after",
                                  "previous_value", "next_value"};

// SPEC-093: where no text is typed, these letters are the arrows.
std::string as_arrow(Key key) {
    switch (key) {
        case Key::F: return "Up";
        case Key::X: return "Down";
        case Key::Z: return "Left";
        case Key::C: return "Right";
        default: return key_name(key);
    }
}

// A `sym` code of SPEC-099: no key of SPEC-111's table, but a character.
bool is_sign(int code) { return key_from_code(code) == Key::None && character_for_code(code, false) != 0; }

}  // namespace

bool action_repeats(const std::string& action) {
    for (const char* name : kRepeating) {
        if (action == name) return true;
    }
    return false;
}

KeyInput::KeyInput(Clock* clock, Place place, KeyListener* listener)
    : clock_(clock), place_(std::move(place)), listener_(listener) {}

KeyInput::~KeyInput() {
    stop_repeat();
    cancel_esc();
    cancel_right();
}

void KeyInput::emit(const KeyEvent& event) { listener_->on_key(event); }

void KeyInput::stop_repeat() {
    if (repeat_timer_ != 0) clock_->cancel(repeat_timer_);
    repeat_timer_ = 0;
    repeat_code_ = 0;
}

void KeyInput::cancel_esc() {
    if (esc_notice_timer_ != 0) clock_->cancel(esc_notice_timer_);
    if (esc_leave_timer_ != 0) clock_->cancel(esc_leave_timer_);
    esc_notice_timer_ = esc_leave_timer_ = 0;
}

void KeyInput::cancel_right() {
    if (right_timer_ != 0) clock_->cancel(right_timer_);
    right_timer_ = 0;
}

void KeyInput::start_repeat(int code) {
    stop_repeat();
    repeat_code_ = code;
    repeat_timer_ = clock_->call_after(kKeyRepeatDelayMs + kKeyRepeatEveryMs, [this] { repeat_once(); });
}

void KeyInput::repeat_once() {
    repeat_timer_ = 0;
    const int code = repeat_code_;
    if (code == 0 || down_.count(code) == 0) return;
    // The place may have changed since the press: the key does what it does there now, if that repeats.
    const std::string place = place_();
    const PlaceKind kind = place_kind(place);
    const Key key = key_from_code(code);
    KeyEvent event;
    event.key = kind == PlaceKind::Typing || kind == PlaceKind::Alone ? key_name(key) : as_arrow(key);
    event.code = code;
    event.action = key_action(place, event.key);
    event.repeat = true;
    if (!action_repeats(event.action)) {
        repeat_code_ = 0;
        return;
    }
    repeat_timer_ = clock_->call_after(kKeyRepeatEveryMs, [this] { repeat_once(); });
    emit(event);
}

void KeyInput::key(int linux_code, bool pressed) {
    const Key key = key_from_code(linux_code);
    const bool sign = is_sign(linux_code);
    if (key == Key::None && !sign) return;                        // SPEC-111: a code no table names

    if (!pressed) {
        if (down_.erase(linux_code) == 0) return;
        if (linux_code == repeat_code_) stop_repeat();
        if (key == Key::Shift) shift_down_ = false;
        if (key == Key::Esc) {
            cancel_esc();
            if (esc_left_) {
                esc_left_ = false;
            } else if (esc_notice_shown_) {
                esc_notice_shown_ = false;
                listener_->on_hold_notice(false);
            } else {
                KeyEvent event;
                event.key = "Esc";
                event.code = linux_code;
                event.action = key_action(place_(), "Esc");
                emit(event);
            }
            return;
        }
        if (linux_code == right_code_ && (right_waiting_ || right_holding_)) {
            cancel_right();
            KeyEvent event;
            event.code = linux_code;
            if (right_holding_) {
                event.key = "RightHold";
                event.action = key_action(place_(), "RightHold");
                event.ended = true;
            } else {
                event.key = "Right";
                event.action = key_action(place_(), "Right");
            }
            right_waiting_ = right_holding_ = false;
            right_code_ = 0;
            emit(event);
        }
        return;
    }

    if (!down_.insert(linux_code).second) return;                 // the keyboard's own repeat
    if (key == Key::Shift) shift_down_ = true;

    if (key == Key::Esc) {
        // SPEC-106: ESC acts when it is let go, or when it has been held.
        stop_repeat();
        cancel_esc();
        esc_notice_shown_ = false;
        esc_left_ = false;
        esc_notice_timer_ = clock_->call_after(kEscNoticeMs, [this] {
            esc_notice_timer_ = 0;
            esc_notice_shown_ = true;
            listener_->on_hold_notice(true);
        });
        esc_leave_timer_ = clock_->call_after(kEscLeaveMs, [this, linux_code] {
            esc_leave_timer_ = 0;
            esc_left_ = true;
            KeyEvent event;
            event.key = "EscHold";
            event.code = linux_code;
            event.action = key_action(place_(), "EscHold");
            emit(event);
        });
        return;
    }

    const std::string place = place_();
    const PlaceKind kind = place_kind(place);
    KeyEvent event;
    event.code = linux_code;
    const char32_t character = character_for_code(linux_code, shift_down_);
    if (kind == PlaceKind::Typing && character != 0) {
        event.key = "Char";                                       // SPEC-099
        event.action = key_action(place, "Char");
        event.character = character;
    } else if (sign) {
        event.key = "Char";                                       // a sign where no text is typed
        event.action = kind == PlaceKind::Alone ? key_action(place, "Char") : "";
    } else {
        event.key = kind == PlaceKind::Typing || kind == PlaceKind::Alone ? key_name(key) : as_arrow(key);
        event.action = key_action(place, event.key);
    }

    // SPEC-096: Right where it can be held waits to see whether it is.
    if (event.key == "Right" && !key_action(place, "RightHold").empty()) {
        stop_repeat();
        cancel_right();
        right_code_ = linux_code;
        right_waiting_ = true;
        right_holding_ = false;
        right_timer_ = clock_->call_after(kRightHoldMs, [this, linux_code] {
            right_timer_ = 0;
            right_waiting_ = false;
            right_holding_ = true;
            KeyEvent held;
            held.key = "RightHold";
            held.code = linux_code;
            held.action = key_action(place_(), "RightHold");
            emit(held);
        });
        return;
    }

    if (action_repeats(event.action)) start_repeat(linux_code);
    else if (key != Key::Shift) stop_repeat();
    emit(event);
}

}  // namespace spz
