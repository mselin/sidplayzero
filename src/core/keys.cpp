// SPDX-License-Identifier: GPL-3.0-or-later
#include "spz/keys.h"

namespace spz {

namespace {

struct Row {
    Key key;
    const char* name;
    int code;
};

// SPEC-111.
const Row kKeys[] = {
    {Key::Esc, "Esc", 1}, {Key::Tab, "Tab", 15}, {Key::Enter, "Enter", 28}, {Key::Backspace, "Backspace", 14},
    {Key::Space, "Space", 57}, {Key::Shift, "Shift", 42},
    {Key::Up, "Up", 103}, {Key::Down, "Down", 108}, {Key::Left, "Left", 105}, {Key::Right, "Right", 106},
    {Key::Home, "Home", 102}, {Key::End, "End", 107}, {Key::PageUp, "PageUp", 104}, {Key::PageDown, "PageDown", 109},
    {Key::Delete, "Delete", 111},
    {Key::PlayPause, "PlayPause", 164}, {Key::Previous, "Previous", 165}, {Key::Next, "Next", 163},
    {Key::Mute, "Mute", 113}, {Key::VolumeDown, "VolumeDown", 114}, {Key::VolumeUp, "VolumeUp", 115},
    {Key::BrightnessDown, "BrightnessDown", 224}, {Key::BrightnessUp, "BrightnessUp", 225},
    {Key::Help, "Help", 138}, {Key::PrintScreen, "PrintScreen", 99},
    {Key::Q, "Q", 16}, {Key::W, "W", 17}, {Key::E, "E", 18}, {Key::R, "R", 19}, {Key::T, "T", 20},
    {Key::Y, "Y", 21}, {Key::U, "U", 22}, {Key::I, "I", 23}, {Key::O, "O", 24}, {Key::P, "P", 25},
    {Key::A, "A", 30}, {Key::S, "S", 31}, {Key::D, "D", 32}, {Key::F, "F", 33}, {Key::G, "G", 34},
    {Key::H, "H", 35}, {Key::J, "J", 36}, {Key::K, "K", 37}, {Key::L, "L", 38},
    {Key::Z, "Z", 44}, {Key::X, "X", 45}, {Key::C, "C", 46}, {Key::V, "V", 47}, {Key::B, "B", 48},
    {Key::N, "N", 49}, {Key::M, "M", 50},
    {Key::D1, "1", 2}, {Key::D2, "2", 3}, {Key::D3, "3", 4}, {Key::D4, "4", 5}, {Key::D5, "5", 6},
    {Key::D6, "6", 7}, {Key::D7, "7", 8}, {Key::D8, "8", 9}, {Key::D9, "9", 10}, {Key::D0, "0", 11},
};

struct Symbol {
    int code;
    char character;
};

// SPEC-099: the `sym` layer arrives as its own key codes.
const Symbol kSymbols[] = {
    {26, '!'}, {27, '@'}, {39, '#'}, {40, '$'}, {41, '%'}, {43, '^'}, {51, '&'}, {52, '*'}, {53, '('}, {94, ')'},
    {55, '~'}, {69, '`'}, {70, '_'}, {71, '-'}, {72, '+'}, {73, '='}, {74, '['}, {75, ']'}, {76, '{'}, {77, '}'},
    {79, ';'}, {80, ':'}, {81, '\''}, {82, '"'}, {83, '<'}, {85, '>'}, {86, '\\'}, {89, '|'}, {90, ','},
    {91, '.'}, {92, '/'}, {93, '?'},
};

}  // namespace

const char* key_name(Key key) {
    for (const Row& row : kKeys) {
        if (row.key == key) return row.name;
    }
    return "";
}

Key key_from_name(const std::string& name) {
    for (const Row& row : kKeys) {
        if (name == row.name) return row.key;
    }
    return Key::None;
}

int key_code(Key key) {
    for (const Row& row : kKeys) {
        if (row.key == key) return row.code;
    }
    return 0;
}

Key key_from_code(int linux_code) {
    for (const Row& row : kKeys) {
        if (row.code == linux_code) return row.key;
    }
    return Key::None;
}

int code_for_character(char32_t character, bool* with_shift) {
    if (with_shift) *with_shift = false;
    if (character == U' ') return key_code(Key::Space);
    if (character >= U'a' && character <= U'z') {
        const char name[2] = {static_cast<char>(character - U'a' + U'A'), 0};
        return key_code(key_from_name(name));
    }
    if (character >= U'A' && character <= U'Z') {
        if (with_shift) *with_shift = true;
        const char name[2] = {static_cast<char>(character), 0};
        return key_code(key_from_name(name));
    }
    if (character >= U'0' && character <= U'9') {
        const char name[2] = {static_cast<char>(character), 0};
        return key_code(key_from_name(name));
    }
    for (const Symbol& symbol : kSymbols) {
        if (static_cast<char32_t>(symbol.character) == character) return symbol.code;
    }
    return 0;
}

char32_t character_for_code(int linux_code, bool shift_down) {
    for (const Symbol& symbol : kSymbols) {
        if (symbol.code == linux_code) return static_cast<char32_t>(symbol.character);
    }
    const Key key = key_from_code(linux_code);
    if (key == Key::Space) return U' ';
    const char* name = key_name(key);
    if (name[0] != 0 && name[1] == 0) {
        const char c = name[0];
        if (c >= 'A' && c <= 'Z') return static_cast<char32_t>(shift_down ? c : c - 'A' + 'a');
        if (c >= '0' && c <= '9') return static_cast<char32_t>(c);
    }
    return 0;
}

}  // namespace spz
