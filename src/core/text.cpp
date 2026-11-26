// SPDX-License-Identifier: GPL-3.0-or-later
// See spz/text.h.
#include "spz/text.h"

namespace spz::text {

std::string trim(const std::string& text, const char* spaces) { return trim_end(trim_start(text, spaces), spaces); }

std::string trim_start(const std::string& text, const char* spaces) {
    const std::size_t first = text.find_first_not_of(spaces);
    return first == std::string::npos ? std::string() : text.substr(first);
}

std::string trim_end(const std::string& text, const char* spaces) {
    const std::size_t last = text.find_last_not_of(spaces);
    return last == std::string::npos ? std::string() : text.substr(0, last + 1);
}

bool starts_with(const std::string& text, const std::string& start) {
    return text.size() >= start.size() && text.compare(0, start.size(), start) == 0;
}

bool ends_with(const std::string& text, const std::string& ending) {
    return text.size() >= ending.size() && text.compare(text.size() - ending.size(), ending.size(), ending) == 0;
}

char ascii_lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

std::string ascii_lower(std::string text) {
    for (char& c : text) c = ascii_lower(c);
    return text;
}

bool same_ignoring_case(const std::string& a, const std::string& b) {
    return a.size() == b.size() && ascii_lower(a) == ascii_lower(b);
}

bool is_tune_name(const std::string& name) {
    if (name.size() < 4) return false;
    const std::size_t at = name.size() - 4;
    return name[at] == '.' && ascii_lower(name[at + 1]) == 's' && ascii_lower(name[at + 2]) == 'i' &&
           ascii_lower(name[at + 3]) == 'd';
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::size_t utf8_char(const char* text, std::size_t size, std::size_t i, std::uint32_t* code) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    std::size_t more;
    std::uint32_t point, least;
    if (c < 0x80) {
        if (code != nullptr) *code = c;
        return 1;
    } else if (c >= 0xC2 && c <= 0xDF) {
        more = 1;
        point = c & 0x1Fu;
        least = 0x80;
    } else if (c >= 0xE0 && c <= 0xEF) {
        more = 2;
        point = c & 0x0Fu;
        least = 0x800;
    } else if (c >= 0xF0 && c <= 0xF4) {
        more = 3;
        point = c & 0x07u;
        least = 0x10000;
    } else {
        return 0;
    }
    if (size - i <= more) return 0;
    for (std::size_t k = 1; k <= more; ++k) {
        const unsigned char next = static_cast<unsigned char>(text[i + k]);
        if ((next & 0xC0) != 0x80) return 0;
        point = (point << 6) | (next & 0x3Fu);
    }
    if (point < least || point > 0x10FFFF || (point >= 0xD800 && point <= 0xDFFF)) return 0;
    if (code != nullptr) *code = point;
    return more + 1;
}

bool is_valid_utf8(const char* text, std::size_t size) {
    std::size_t i = 0;
    while (i < size) {
        const std::size_t length = utf8_char(text, size, i);
        if (length == 0) return false;
        i += length;
    }
    return true;
}

void put_utf8(std::uint32_t code, std::string* out) {
    if (code < 0x80) {
        out->push_back(static_cast<char>(code));
    } else if (code < 0x800) {
        out->push_back(static_cast<char>(0xC0 | (code >> 6)));
        out->push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code < 0x10000) {
        out->push_back(static_cast<char>(0xE0 | (code >> 12)));
        out->push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out->push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
        out->push_back(static_cast<char>(0xF0 | (code >> 18)));
        out->push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
        out->push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out->push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
}

std::string latin1_to_utf8(const std::string& text) {
    std::string out;
    out.reserve(text.size() + text.size() / 4);
    for (const char c : text) put_utf8(static_cast<unsigned char>(c), &out);
    return out;
}

std::size_t utf8_length(const std::string& text) {
    std::size_t n = 0;
    for (const char c : text) {
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
    }
    return n;
}

std::string utf8_prefix(const std::string& text, std::size_t characters) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) {
            if (n == characters) return text.substr(0, i);
            ++n;
        }
    }
    return text;
}

}  // namespace spz::text
