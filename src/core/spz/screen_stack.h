// SPDX-License-Identifier: GPL-3.0-or-later
// The stack of open screens (SPEC-090). The browser, or the empty screen in its place, is at the bottom;
// help and panels open over a screen and are not in the stack.
#pragma once

#include <string>
#include <vector>

namespace spz {

class ScreenStack {
public:
    // The stack at start: the browser alone.
    ScreenStack();

    // The open screens, the bottom one first, by their names in the screen description (SPEC-254):
    // "browser", "empty", "playing", "search", "playlists", "playlist", "settings", "hvsc", "about",
    // "selftest".
    std::vector<std::string> screens() const;
    // The screen on top.
    std::string top() const;

    // A key that opens a screen: puts it on top. If the screen is in the stack already, the stack returns
    // to it instead, closing those above it; so no screen is ever in the stack twice.
    void open(const std::string& screen);

    // A short ESC: closes the top screen. With only the bottom screen open it closes nothing and returns
    // false; there ESC belongs to the screen (in the browser it is the "parent folder" key).
    bool close_top();

    // SPEC-048: which of the two the bottom screen is, "browser" or "empty". The screens above it stay.
    void set_bottom(const std::string& screen);

private:
    std::vector<std::string> screens_;
};

}  // namespace spz
