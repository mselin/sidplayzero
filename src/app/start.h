// SPDX-License-Identifier: GPL-3.0-or-later
// What the program needs before its screens: the lock of SPEC-200, and the start time it measures. What a
// device test asks for (SPEC-260) is the composition's: src/app/compose_selftest.cpp.
#pragma once

namespace spz::app {

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
