// SPDX-License-Identifier: GPL-3.0-or-later
// STUB: not built yet (Phase 5).
#include "spz/screen_off.h"

#include "spz/not_built.h"

namespace spz {

ScreenOff::ScreenOff(Clock* clock, Executor* quick, Backlight* backlight, StateFile* state)
    : clock_(clock), quick_(quick), backlight_(backlight), state_(state) {
    (void)clock_;
    (void)quick_;
    (void)backlight_;
    (void)state_;
}

ScreenOff::~ScreenOff() {}

void ScreenOff::at_start() { SPZ_NOT_BUILT("SPEC-207"); }
bool ScreenOff::available() const { SPZ_NOT_BUILT("SPEC-207"); }
void ScreenOff::set_after_s(int) { SPZ_NOT_BUILT("SPEC-207"); }
void ScreenOff::set_playing(bool) { SPZ_NOT_BUILT("SPEC-207"); }
bool ScreenOff::key() { SPZ_NOT_BUILT("SPEC-207"); }
void ScreenOff::at_exit() { SPZ_NOT_BUILT("SPEC-207"); }
bool ScreenOff::is_off() const { SPZ_NOT_BUILT("SPEC-207"); }

}  // namespace spz
