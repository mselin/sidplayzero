// SPDX-License-Identifier: GPL-3.0-or-later
// STUB: not built yet (Phase 5).
#include "spz/key_input.h"

#include <utility>

#include "spz/not_built.h"

namespace spz {

bool action_repeats(const std::string&) { SPZ_NOT_BUILT("SPEC-045"); }

KeyInput::KeyInput(Clock* clock, Place place, KeyListener* listener)
    : clock_(clock), place_(std::move(place)), listener_(listener) {}
KeyInput::~KeyInput() {}

void KeyInput::key(int, bool) {
    (void)clock_;
    (void)listener_;
    SPZ_NOT_BUILT("SPEC-111");
}

}  // namespace spz
