// SPDX-License-Identifier: GPL-3.0-or-later
// The two rules for the path of a tune inside the collection (SPEC-154), and the address of a file under a
// base address.
#include "spz/paths.h"

#include <cstddef>
#include <cstdint>

#include "spz/text.h"

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
    return path.size() >= 5 && text::is_tune_name(path);
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
        std::uint32_t code = 0;
        const std::size_t length = text::utf8_char(text, i, &code);
        if (length == 0) return false;
        if (code >= 0x80 && code <= 0x9F) return false;   // the C1 control characters
        i += length;
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

std::string join_url(const std::string& base, const std::string& path) {
    std::size_t end = base.size();
    while (end > 0 && base[end - 1] == '/') --end;
    return base.substr(0, end) + path;
}

}  // namespace spz
