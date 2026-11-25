// SPDX-License-Identifier: GPL-3.0-or-later
// STUB (TF-005). The least of an app that every level of tests can run: one screen, a held ESC that
// leaves, a lock, and the start and exit times a device test asks for. Phase 5 replaces this.
#pragma once

#include <memory>
#include <string>

#include "spz/clock.h"
#include "spz/json.h"
#include "ui/stub_screen.h"

namespace spz::app {

// The collection root (SPEC-040, in small): HVSC_BASE, else C64Music in the home folder.
std::string collection_root();
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

// SPEC-260, in small: while a request file is in the state folder, start and exit times are appended to
// the results file beside it.
void record_for_selftest(const std::string& key, const std::string& value);

class StubApp {
public:
    explicit StubApp(Clock* clock);
    ~StubApp();

    // A key went down or came up, as a Linux key code. A held ESC leaves (SPEC-106).
    void key(int linux_code, bool pressed);
    // Asked to leave by a signal (SPEC-201).
    void leave() { leaving_ = true; }
    bool leaving() const { return leaving_; }

    Json describe() const { return screen_->describe(); }

private:
    Clock* clock_;
    std::unique_ptr<ui::StubScreen> screen_;
    Clock::TimerId notice_timer_ = 0;
    Clock::TimerId leave_timer_ = 0;
    bool esc_down_ = false;
    bool leaving_ = false;
};

}  // namespace spz::app
