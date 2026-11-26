// SPDX-License-Identifier: GPL-3.0-or-later
// A field that text is typed into (SPEC-099): the Search field, and the field of a text prompt. It holds
// the text and the cursor and takes the key map's actions for a field; drawing it is the screen's work.
#pragma once

#include <cstddef>
#include <string>

namespace spz {

// SPEC-099: how many characters a field holds at most.
const std::size_t kSearchFieldLimit = 64;
const std::size_t kPlaylistNameLimit = 40;
const std::size_t kPathFieldLimit = 255;      // a path or an address
const std::size_t kLengthFieldLimit = 5;      // a length, such as "59:59"

class TextField {
public:
    // A field of at most `limit` characters, holding `text` (UTF-8) with the cursor after its last
    // character.
    explicit TextField(std::size_t limit, const std::string& text = "");

    // Does an action of the key map: "type" with the character; "delete_before" (Backspace) and
    // "delete_after" (Delete); "cursor_left", "cursor_right", "cursor_first" and "cursor_last". Returns
    // whether the text or the cursor changed. A character that would take the field past its limit is not
    // added; a cursor at an end does not move past it; any other action changes nothing.
    bool apply(const std::string& action, char32_t character = 0);

    // The text, as UTF-8.
    std::string text() const;
    // How many characters the text has. A character is one Unicode code point, whatever its bytes.
    std::size_t length() const;
    // How many characters are before the cursor: 0 at the start, length() at the end.
    std::size_t cursor() const;
    // The most characters the field holds.
    std::size_t limit() const;

private:
    std::size_t limit_;
    std::u32string chars_;        // one element for each character
    std::size_t cursor_ = 0;
};

}  // namespace spz
