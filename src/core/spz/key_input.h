// SPDX-License-Identifier: GPL-3.0-or-later
// The one place where a key becomes an action (SPEC-111). The device's keyboard and the desktop build's
// window both hand their presses and releases here, as Linux key codes. What comes out is the key map's
// action for the place the keys go to now (spz/keymap.h), or a typed character (SPEC-099).
#pragma once

#include <functional>
#include <set>
#include <string>

#include "spz/clock.h"
#include "spz/types.h"

namespace spz {

// SPEC-045: the app's own key repeat. The keyboard's is ignored.
const Millis kKeyRepeatDelayMs = 400;    // a held key starts to repeat after this
const Millis kKeyRepeatEveryMs = 40;     // and then acts 25 times a second
// SPEC-106: ESC.
const Millis kEscNoticeMs = 500;         // released before this, ESC is "back"; held this long, the notice shows
const Millis kEscLeaveMs = 1000;         // held this long, the app leaves
// SPEC-096: Right on the now-playing screen, with the part `skip`.
const Millis kRightHoldMs = 400;         // held this long, Right skips ahead and does not change the subtune

// What a key became.
struct KeyEvent {
    std::string key;         // its name in the key map: "Enter", "4", "Up", "Char", "EscHold", "RightHold" ...
    int code = 0;            // the Linux key code that arrived (the key step of SPEC-260 records it)
    std::string action;      // the key map's action for it in the place: "back", "type" ...; "" if it has none
    char32_t character = 0;  // with the action "type": the character typed
    bool repeat = false;     // made by the app's own key repeat, not by a press (SPEC-045)
    bool ended = false;      // "skip_ahead" lasts while Right stays down: true when the key has come up
};

class KeyListener {
public:
    virtual ~KeyListener() = default;
    // A key was pressed, repeated, or (for ESC, and Right where it can be held) let go in time.
    virtual void on_key(const KeyEvent& event) = 0;
    // SPEC-106: true when ESC has been held for 500 ms, so that MSG_HOLD_TO_LEAVE shows; false when it is
    // let go after that and before the app leaves, so that the notice goes away.
    virtual void on_hold_notice(bool shown) = 0;
};

// SPEC-045: whether an action repeats while its key is held: moving in a list ("up", "down", "page_up",
// "page_down"), moving the cursor and deleting in a text field, and stepping a value. No other does.
bool action_repeats(const std::string& action);

class KeyInput {
public:
    // Asked at every key for the place of the key map whose keys apply now: the screen description's
    // `place` (SPEC-254).
    using Place = std::function<std::string()>;

    // Timers for the repeat and the holds are set on `clock` and fire on the UI thread.
    KeyInput(Clock* clock, Place place, KeyListener* listener);
    ~KeyInput();
    KeyInput(const KeyInput&) = delete;
    KeyInput& operator=(const KeyInput&) = delete;

    // A key went down (true) or came up (false). The rules, each from its item:
    //
    // - SPEC-111: a code the specification's table names gives one event for each press, with the key's
    //   name, its code, and the key map's action in the place of that moment ("" if none). A code that
    //   neither that table nor SPEC-099 names gives nothing. A second press of a key that is already down
    //   gives nothing: that is the keyboard's own repeat.
    // - SPEC-099: in a place where text is typed, a key that types a character gives the key "Char" with
    //   the action of the place's "Char" row and the character: a letter in lower case, or its capital
    //   while Shift (code 42) is down; a digit or a space as itself; a `sym` code as its sign. Where no
    //   text is typed a `sym` code, which is no key of the table, gives no action; in the key step, whose
    //   row for "Any" takes every key, it gives that row's action with the key "Char" (SPEC-260).
    // - SPEC-093: where no text is typed, the letters F, X, Z and C are the keys Up, Down, Left and Right.
    // - SPEC-045: while a key whose action repeats stays down, the event comes again with `repeat` set,
    //   first kKeyRepeatDelayMs + kKeyRepeatEveryMs after the press and then every kKeyRepeatEveryMs.
    //   That is how a key held for one second acts 16 times, the number of SPEC-045's oracle: once for
    //   the press, and 15 times more from 440 ms to 1,000 ms.
    // - SPEC-106: ESC acts when it is let go. Let go before kEscNoticeMs it gives the key "Esc". Held
    //   that long it gives on_hold_notice(true); let go after that, on_hold_notice(false) and no key.
    //   Held for kEscLeaveMs it gives the key "EscHold", once, and nothing when it comes up.
    // - SPEC-096: in a place that has a row for "RightHold" whose part is built, Right acts when it is let
    //   go, if that is before kRightHoldMs. Held that long it gives the key "RightHold", and gives it
    //   again with `ended` set when it comes up; the key "Right" is then not given at all.
    void key(int linux_code, bool pressed);

private:
    void emit(const KeyEvent& event);
    void start_repeat(int code);
    void repeat_once();
    void stop_repeat();
    void cancel_esc();
    void cancel_right();

    Clock* clock_;
    Place place_;
    KeyListener* listener_;
    std::set<int> down_;                  // the codes that are down now
    bool shift_down_ = false;
    // SPEC-045: the one key that repeats, the last one pressed whose action repeats.
    int repeat_code_ = 0;
    Clock::TimerId repeat_timer_ = 0;
    // SPEC-106: ESC while it is down.
    Clock::TimerId esc_notice_timer_ = 0;
    Clock::TimerId esc_leave_timer_ = 0;
    bool esc_notice_shown_ = false;
    bool esc_left_ = false;
    // SPEC-096: Right where it can be held.
    int right_code_ = 0;                  // the code that is Right while it waits or skips: Right, or C
    Clock::TimerId right_timer_ = 0;
    bool right_waiting_ = false;
    bool right_holding_ = false;
};

}  // namespace spz
