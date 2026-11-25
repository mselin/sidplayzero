// SPDX-License-Identifier: GPL-3.0-or-later
// Key presses and releases, as Linux key codes (SPEC-111). On the device they come from the keyboard's
// input device, which is read without grabbing it (REQ-127); in a desktop window, from the window.
#pragma once

#include <functional>
#include <string>

namespace spz::platform {

class Keyboard {
public:
    // Called for each press (true) and release (false). The keyboard's own repeats are not passed on.
    using Handler = std::function<void(int linux_code, bool pressed)>;

    ~Keyboard();

    // Opens the device's keyboard: the path in SIDPLAYZERO_KEYBOARD, else the CardputerZero's own, else
    // the first input device that has the keys A to Z. In a window build there is nothing to open.
    bool open(std::string* why);

    // Hands every event that has arrived since the last call to `handler`. Never waits.
    void poll(const Handler& handler);

private:
    int fd_ = -1;
};

}  // namespace spz::platform
