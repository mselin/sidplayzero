// SPDX-License-Identifier: GPL-3.0-or-later
// What the program needs before its screens: the lock of SPEC-200, and the start time it measures. What a
// device test asks for (SPEC-260) is the composition's: src/app/compose_selftest.cpp.
#pragma once

#include <string>
#include <vector>

#include "platform/display.h"
#include "spz/clock.h"

namespace spz::app {

// SPEC-200, step 3, as the program and the simulator's scripts both do it: LVGL, on `clock`'s milliseconds,
// with its display of `kind` opened. False, with why, if the display cannot be opened. `clock` outlives LVGL.
bool start_program(Clock* clock, platform::DisplayKind kind, std::string* why);

// SPEC-201: SIGTERM and SIGINT are counted, not obeyed, from here on: the program begins its exit at its
// next turn of the UI loop (exit_signals() is above 0 then).
void count_exit_signals();
int exit_signals();
// SPEC-201: once the exit has begun, a second signal ends the process at once.
void obey_exit_signals();

// SPEC-103: the engine's libraries with their versions, "libsidplayfp 3.1.2" and "libresidfp 1.3.0".
std::vector<std::string> engine_libraries();

// The lock of SPEC-200: held while the app runs. take() is false if another SidPlayZero holds it. It is
// spz::lock_path() of the folders SPEC-134 gives the environment: the state folder the composition keeps
// state.ini in, so a relative XDG_STATE_HOME is ignored here as it is there. The folders are made for the
// user only.
class Lock {
public:
    ~Lock();
    bool take();
    void release();

private:
    int fd_ = -1;
};

// Milliseconds since this process was started, as the system records it (SPEC-200's start time).
long long ms_since_process_start();

}  // namespace spz::app
