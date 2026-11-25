// SPDX-License-Identifier: GPL-3.0-or-later
// The keys of SPEC-111: their names, as the key map and the test scripts use them, and their Linux key
// codes, as the device's keyboard sends them. This table is the only place the two meet.
#pragma once

#include <string>

namespace spz {

enum class Key {
    None,
    Esc, Tab, Enter, Backspace, Space, Shift,
    Up, Down, Left, Right, Home, End, PageUp, PageDown, Delete,
    PlayPause, Previous, Next, Mute, VolumeDown, VolumeUp, BrightnessDown, BrightnessUp, Help, PrintScreen,
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    D0, D1, D2, D3, D4, D5, D6, D7, D8, D9,
};

// "Esc", "PageUp", "A", "4": the name in docs/keymap.tsv. "" for Key::None.
const char* key_name(Key key);
// Key::None if there is no key of that name.
Key key_from_name(const std::string& name);
// The Linux key code, or 0 for Key::None.
int key_code(Key key);
// Key::None for a code SPEC-111 does not name; such a key is ignored.
Key key_from_code(int linux_code);

// The key code that types a character (SPEC-099), and whether Shift is held with it. 0 if none does.
int code_for_character(char32_t character, bool* with_shift);
// The character a key code types, given whether Shift is down. 0 if it types nothing.
char32_t character_for_code(int linux_code, bool shift_down);

}  // namespace spz
