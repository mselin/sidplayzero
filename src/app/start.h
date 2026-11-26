// SPDX-License-Identifier: GPL-3.0-or-later
// What the program needs before its screens: the folders it starts from and the lock of SPEC-200. In
// small, until the areas of SPEC-040, SPEC-134 and SPEC-200 take them over. What a device test asks for
// (SPEC-260) is the composition's: src/app/compose_selftest.cpp.
#pragma once

#include <string>

namespace spz::app {

// The collection root (SPEC-040, in small): HVSC_BASE, else C64Music in the home folder.
std::string collection_root();
// Whether HVSC_BASE names the root (SPEC-040): set, and not empty.
bool root_from_variable();
// The home folder: HOME, else "/".
std::string home_folder();
// The app's state folder (SPEC-134): $XDG_STATE_HOME/sidplayzero, else ~/.local/state/sidplayzero.
std::string state_folder();

// The lock of SPEC-200: held while the app runs. take() is false if another SidPlayZero holds it.
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
