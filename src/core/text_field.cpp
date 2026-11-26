// SPDX-License-Identifier: GPL-3.0-or-later
// A field that text is typed into (SPEC-099).
#include "spz/text_field.h"

#include "spz/text.h"

namespace spz {

namespace {

// UTF-8 to characters. A byte that does not begin a valid sequence is taken as the character of its value,
// so that no byte of a value is lost.
std::u32string decode(const std::string& text) {
    std::u32string out;
    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char first = static_cast<unsigned char>(text[i]);
        int more = first < 0x80 ? 0 : (first >> 5) == 0x6 ? 1 : (first >> 4) == 0xE ? 2 : (first >> 3) == 0x1E ? 3 : -1;
        char32_t c = more == 0 ? first : more == 1 ? (first & 0x1Fu) : more == 2 ? (first & 0x0Fu) : (first & 0x07u);
        bool ok = more >= 0 && i + static_cast<std::size_t>(more) < text.size();
        for (int k = 1; ok && k <= more; ++k) {
            const unsigned char next = static_cast<unsigned char>(text[i + static_cast<std::size_t>(k)]);
            if ((next & 0xC0u) != 0x80u) ok = false;
            else c = (c << 6) | (next & 0x3Fu);
        }
        if (!ok) {
            out.push_back(first);
            ++i;
        } else {
            out.push_back(c);
            i += static_cast<std::size_t>(more) + 1;
        }
    }
    return out;
}

}  // namespace

TextField::TextField(std::size_t limit, const std::string& text) : limit_(limit), chars_(decode(text)) {
    cursor_ = chars_.size();                      // SPEC-099, FIND-024: the cursor at the end
}

bool TextField::apply(const std::string& action, char32_t character) {
    if (action == "type") {
        if (character == 0 || chars_.size() >= limit_) return false;
        chars_.insert(chars_.begin() + static_cast<std::ptrdiff_t>(cursor_), character);
        ++cursor_;
        return true;
    }
    if (action == "delete_before") {
        if (cursor_ == 0) return false;
        chars_.erase(chars_.begin() + static_cast<std::ptrdiff_t>(cursor_ - 1));
        --cursor_;
        return true;
    }
    if (action == "delete_after") {
        if (cursor_ >= chars_.size()) return false;
        chars_.erase(chars_.begin() + static_cast<std::ptrdiff_t>(cursor_));
        return true;
    }
    if (action == "cursor_left") {
        if (cursor_ == 0) return false;
        --cursor_;
        return true;
    }
    if (action == "cursor_right") {
        if (cursor_ >= chars_.size()) return false;
        ++cursor_;
        return true;
    }
    if (action == "cursor_first") {
        if (cursor_ == 0) return false;
        cursor_ = 0;
        return true;
    }
    if (action == "cursor_last") {
        if (cursor_ == chars_.size()) return false;
        cursor_ = chars_.size();
        return true;
    }
    return false;
}

std::string TextField::text() const {
    std::string out;
    for (const char32_t c : chars_) text::put_utf8(c, &out);
    return out;
}

std::size_t TextField::length() const { return chars_.size(); }
std::size_t TextField::cursor() const { return cursor_; }
std::size_t TextField::limit() const { return limit_; }

}  // namespace spz
