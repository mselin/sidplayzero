// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/keyboard.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <utility>

#if SPZ_DISPLAY_WINDOW
#include <SDL2/SDL.h>
#else
#include <fcntl.h>
#include <linux/input.h>
#include <unistd.h>

#include "platform/input_setup.h"
#endif

namespace spz::platform {

#if SPZ_DISPLAY_WINDOW

namespace {

std::mutex g_mutex;
std::deque<std::pair<int, bool>> g_events;
bool g_watching = false;

// A desktop key as the Linux key code the device's keyboard would send. F1 to F10 stand for the keys a
// desktop keyboard may not have (SPEC-111).
int linux_code(SDL_Scancode key) {
    switch (key) {
        case SDL_SCANCODE_ESCAPE: return 1;
        case SDL_SCANCODE_TAB: return 15;
        case SDL_SCANCODE_RETURN: return 28;
        case SDL_SCANCODE_BACKSPACE: return 14;
        case SDL_SCANCODE_SPACE: return 57;
        case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT: return 42;
        case SDL_SCANCODE_UP: return 103;
        case SDL_SCANCODE_DOWN: return 108;
        case SDL_SCANCODE_LEFT: return 105;
        case SDL_SCANCODE_RIGHT: return 106;
        case SDL_SCANCODE_HOME: return 102;
        case SDL_SCANCODE_END: return 107;
        case SDL_SCANCODE_PAGEUP: return 104;
        case SDL_SCANCODE_PAGEDOWN: return 109;
        case SDL_SCANCODE_DELETE: return 111;
        case SDL_SCANCODE_F1: return 164;
        case SDL_SCANCODE_F2: return 165;
        case SDL_SCANCODE_F3: return 163;
        case SDL_SCANCODE_F4: return 113;
        case SDL_SCANCODE_F5: return 114;
        case SDL_SCANCODE_F6: return 115;
        case SDL_SCANCODE_F7: return 224;
        case SDL_SCANCODE_F8: return 225;
        case SDL_SCANCODE_F9: return 138;
        case SDL_SCANCODE_F10: return 99;
        case SDL_SCANCODE_0: return 11;
        default: break;
    }
    if (key >= SDL_SCANCODE_1 && key <= SDL_SCANCODE_9) return 2 + (key - SDL_SCANCODE_1);
    static const int letters[26] = {30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50,
                                    49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44};
    if (key >= SDL_SCANCODE_A && key <= SDL_SCANCODE_Z) return letters[key - SDL_SCANCODE_A];
    return 0;
}

int watch(void*, SDL_Event* event) {
    if ((event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) && event->key.repeat == 0) {
        const int code = linux_code(event->key.keysym.scancode);
        if (code != 0) {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_events.emplace_back(code, event->type == SDL_KEYDOWN);
        }
    }
    return 1;
}

}  // namespace

Keyboard::~Keyboard() = default;

bool Keyboard::open(std::string*) {
    if (!g_watching) {
        SDL_AddEventWatch(watch, nullptr);
        g_watching = true;
    }
    return true;
}

void Keyboard::poll(const Handler& handler) {
    std::deque<std::pair<int, bool>> events;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        events.swap(g_events);
    }
    for (const auto& event : events) handler(event.first, event.second);
}

#else  // the device, or a build with no window: Linux input events

Keyboard::~Keyboard() {
    if (fd_ >= 0) ::close(fd_);
}

bool Keyboard::open(std::string* why) {
    const char* named = std::getenv("SIDPLAYZERO_KEYBOARD");
    const std::string path = choose_keyboard(list_input_devices(), named ? named : "");
    if (!path.empty()) fd_ = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0) {
        if (why) *why = "no keyboard could be opened";
        return false;
    }
    return true;   // never taken for this app alone: the launcher and other apps read the same device
}

void Keyboard::poll(const Handler& handler) {
    if (fd_ < 0) return;
    input_event event;
    for (;;) {
        const ssize_t got = ::read(fd_, &event, sizeof event);
        if (got != static_cast<ssize_t>(sizeof event)) break;
        if (event.type == EV_KEY && event.value != 2) handler(event.code, event.value == 1);
    }
}

#endif

}  // namespace spz::platform
