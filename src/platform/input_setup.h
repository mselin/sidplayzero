// SPDX-License-Identifier: GPL-3.0-or-later
// The device's input set-up (SPEC-110, SPEC-111): which input device the keys are read from, and the one
// LVGL input device the app makes, a keypad. There is no pointer, touch, encoder or button input device:
// every screen takes its keys from the key map.
//
// The keyboard is the path in SIDPLAYZERO_KEYBOARD, else the CardputerZero's own keyboard, else the first
// input device that reports the keys A to Z. It is read without being taken from the launcher and the
// other apps, which read the same device.
#pragma once

#include <string>
#include <vector>

#include "lvgl.h"

namespace spz::platform {

// The CardputerZero's own keyboard (SPEC-111).
extern const char kCardputerKeyboard[];

// One input device, and what it reports.
struct InputDevice {
    std::string path;
    bool letters = false;    // the keys A to Z
    bool buttons = false;    // a mouse's buttons, or a touch screen's touch
    bool relative = false;   // movement, as a mouse reports it
    bool absolute = false;   // positions, as a touch screen reports them
};

// The input devices of this machine: /dev/input/event*, and the CardputerZero's keyboard by its path when
// it is there. Each is opened to ask what it reports, and closed again.
std::vector<InputDevice> list_input_devices();

// SPEC-111's choice among `devices`: `named` (SIDPLAYZERO_KEYBOARD's value) if it is not empty; else the
// CardputerZero's keyboard if it is among them; else the first that reports the keys A to Z; else "".
std::string choose_keyboard(const std::vector<InputDevice>& devices, const std::string& named);

// Makes the app's one LVGL input device: a keypad, read by `read`. LVGL must have been started.
lv_indev_t* make_keypad(lv_indev_read_cb_t read, void* user_data);

}  // namespace spz::platform
