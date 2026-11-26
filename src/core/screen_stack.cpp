// SPDX-License-Identifier: GPL-3.0-or-later
// The stack of open screens (SPEC-090).
#include "spz/screen_stack.h"

#include <algorithm>

namespace spz {

ScreenStack::ScreenStack() : screens_{"browser"} {}

std::vector<std::string> ScreenStack::screens() const { return screens_; }

std::string ScreenStack::top() const { return screens_.back(); }

void ScreenStack::open(const std::string& screen) {
    // The bottom screen is the browser or the empty screen in its place: opening "browser" returns there.
    const bool bottom = screen == "browser" || screen == "empty";
    auto at = bottom ? screens_.begin() : std::find(screens_.begin(), screens_.end(), screen);
    if (at != screens_.end()) {
        screens_.erase(at + 1, screens_.end());
        return;
    }
    screens_.push_back(screen);
}

bool ScreenStack::close_top() {
    if (screens_.size() <= 1) return false;
    screens_.pop_back();
    return true;
}

void ScreenStack::set_bottom(const std::string& screen) { screens_.front() = screen; }

}  // namespace spz
