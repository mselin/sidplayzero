// SPDX-License-Identifier: GPL-3.0-or-later
// The two rules for the path of a tune inside the collection (SPEC-154).
#include "spz/paths.h"

#include <cstddef>
#include <cstdint>

namespace spz {

namespace {

// The shape both rules share: starts with "/", no empty part, no part that is "." or "..", and ends in
// ".sid" in any case.
bool has_tune_shape(const std::string& path) {
    if (path.empty() || path[0] != '/') return false;
    std::size_t start = 1;
    for (;;) {
        const std::size_t slash = path.find('/', start);
        const std::size_t end = slash == std::string::npos ? path.size() : slash;
        const std::size_t length = end - start;
        if (length == 0) return false;
        if (length == 1 && path[start] == '.') return false;
        if (length == 2 && path[start] == '.' && path[start + 1] == '.') return false;
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    if (path.size() < 5) return false;
    const char* end = path.c_str() + path.size() - 4;
    auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; };
    return end[0] == '.' && lower(end[1]) == 's' && lower(end[2]) == 'i' && lower(end[3]) == 'd';
}

// Valid UTF-8 (no overlong forms, no surrogates, nothing past U+10FFFF) with no control character (C0,
// DEL, C1) and no backslash.
bool is_clean_utf8(const std::string& text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            if (c < 0x20 || c == 0x7F || c == '\\') return false;
            ++i;
            continue;
        }
        std::size_t more;
        std::uint32_t code, least;
        if (c >= 0xC2 && c <= 0xDF) {
            more = 1;
            code = c & 0x1F;
            least = 0x80;
        } else if (c >= 0xE0 && c <= 0xEF) {
            more = 2;
            code = c & 0x0F;
            least = 0x800;
        } else if (c >= 0xF0 && c <= 0xF4) {
            more = 3;
            code = c & 0x07;
            least = 0x10000;
        } else {
            return false;
        }
        if (text.size() - i <= more) return false;
        for (std::size_t k = 1; k <= more; ++k) {
            const unsigned char next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xC0) != 0x80) return false;
            code = (code << 6) | (next & 0x3F);
        }
        if (code < least || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) return false;
        if (code >= 0x80 && code <= 0x9F) return false;   // the C1 control characters
        i += more + 1;
    }
    return true;
}

}  // namespace

bool is_hvsc_path(const std::string& path) {
    if (path.size() > 255) return false;
    for (const char c : path) {
        const bool allowed = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
                             c == '-' || c == '.' || c == '/';
        if (!allowed) return false;
    }
    return has_tune_shape(path);
}

bool is_collection_path(const std::string& path) {
    if (path.size() > 1024) return false;
    return is_clean_utf8(path) && has_tune_shape(path);
}

}  // namespace spz
