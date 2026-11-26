// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/input_setup.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>

namespace spz::platform {

const char kCardputerKeyboard[] = "/dev/input/by-path/platform-3f804000.i2c-event";

namespace {

const std::size_t kBitsPerLong = 8 * sizeof(unsigned long);

bool has_bit(const unsigned long* bits, int code) {
    return (bits[static_cast<std::size_t>(code) / kBitsPerLong] >> (static_cast<std::size_t>(code) % kBitsPerLong)) & 1UL;
}

bool describe(const std::string& path, InputDevice* out) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return false;
    unsigned long types[(EV_MAX + kBitsPerLong) / kBitsPerLong] = {};
    unsigned long keys[(KEY_MAX + kBitsPerLong) / kBitsPerLong] = {};
    const bool asked = ::ioctl(fd, EVIOCGBIT(0, sizeof types), types) >= 0;
    if (asked && has_bit(types, EV_KEY)) ::ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys);
    ::close(fd);
    if (!asked) return false;
    out->path = path;
    out->letters = true;
    for (int code : {KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I, KEY_O, KEY_P, KEY_A, KEY_S, KEY_D,
                     KEY_F, KEY_G, KEY_H, KEY_J, KEY_K, KEY_L, KEY_Z, KEY_X, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M}) {
        if (!has_bit(keys, code)) out->letters = false;
    }
    out->buttons = has_bit(keys, BTN_LEFT) || has_bit(keys, BTN_TOUCH);
    out->relative = has_bit(types, EV_REL);
    out->absolute = has_bit(types, EV_ABS);
    return true;
}

}  // namespace

std::vector<InputDevice> list_input_devices() {
    std::vector<InputDevice> found;
    InputDevice device;
    if (describe(kCardputerKeyboard, &device)) found.push_back(device);
    std::vector<std::string> names;
    if (DIR* folder = ::opendir("/dev/input")) {
        while (const dirent* entry = ::readdir(folder)) {
            if (std::strncmp(entry->d_name, "event", 5) == 0) names.push_back(entry->d_name);
        }
        ::closedir(folder);
    }
    // event2 before event10.
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
        return a.size() != b.size() ? a.size() < b.size() : a < b;
    });
    for (const std::string& name : names) {
        InputDevice one;
        if (describe("/dev/input/" + name, &one)) found.push_back(one);
    }
    return found;
}

std::string choose_keyboard(const std::vector<InputDevice>& devices, const std::string& named) {
    if (!named.empty()) return named;
    for (const InputDevice& device : devices) {
        if (device.path == kCardputerKeyboard) return device.path;
    }
    for (const InputDevice& device : devices) {
        if (device.letters) return device.path;
    }
    return "";
}

lv_indev_t* make_keypad(lv_indev_read_cb_t read, void* user_data) {
    lv_indev_t* keypad = lv_indev_create();
    if (keypad == nullptr) return nullptr;
    lv_indev_set_type(keypad, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(keypad, read);
    lv_indev_set_user_data(keypad, user_data);
    return keypad;
}

}  // namespace spz::platform
