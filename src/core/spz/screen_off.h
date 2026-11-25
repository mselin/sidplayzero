// SPDX-License-Identifier: GPL-3.0-or-later
// The screen while a tune plays (SPEC-207, the part `screen-off`): with the setting screen_off_after above
// 0, the app turns the backlight off when that many seconds have passed without a key while a tune plays,
// and puts it back at the next key, when playback pauses or stops, and when the app exits. The brightness
// to put back is written to state.ini before the backlight goes off, so that a run that is killed with
// the screen off is followed by a start that puts it back.
#pragma once

#include "spz/clock.h"
#include "spz/executor.h"
#include "spz/state_file.h"
#include "spz/system.h"

namespace spz {

class ScreenOff {
public:
    // `clock` counts the seconds; `quick` is the quick worker (SPEC-022), which does everything that
    // touches the backlight or the state file; `state` holds backlight_saved.
    ScreenOff(Clock* clock, Executor* quick, Backlight* backlight, StateFile* state);
    // Forgets its timer. It does not touch the backlight: the exit calls at_exit() first.
    ~ScreenOff();
    ScreenOff(const ScreenOff&) = delete;
    ScreenOff& operator=(const ScreenOff&) = delete;

    // At start, on the calling thread, after the state file is read (SPEC-200, step 2): if backlight_saved
    // is set, a run was killed with the screen off: sets the backlight to it and clears it. Also asks
    // the backlight whether it can be changed.
    void at_start();

    // Whether the backlight can be changed, as at_start() found. When it cannot, the setting's row reads
    // "Not available on this device", the setting stays 0, and nothing here ever touches the backlight.
    bool available() const;

    // The setting screen_off_after, in seconds; 0 for never. Called at start and whenever it changes.
    void set_after_s(int seconds);
    // From the player: true while a tune plays, false when playback pauses or stops. Going to false with
    // the screen off puts the brightness back and clears backlight_saved.
    void set_playing(bool playing);

    // A key was pressed, whatever key. Returns true when the screen was off: the key has turned it back on
    // (the brightness is put back and backlight_saved cleared) and must do nothing else. Returns false
    // otherwise: the key does what it does, and the seconds without a key are counted from now.
    bool key();

    // At exit (SPEC-201), on the calling thread: puts the brightness back if the app has the screen off,
    // and clears backlight_saved, before state.ini is written for the last time.
    void at_exit();

    // Whether the app has the screen off.
    bool is_off() const;

private:
    Clock* clock_;
    Executor* quick_;
    Backlight* backlight_;
    StateFile* state_;
};

}  // namespace spz
