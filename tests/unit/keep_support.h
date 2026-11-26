// SPDX-License-Identifier: GPL-3.0-or-later
// What the tests of the "keep" area share: looking at the real folder behind the fake Files directly,
// so that a test sees what is on the card and not what the code under test says is there.
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "fakes.h"
#include "spz/types.h"

namespace spz::test {

// A file's bytes, read without the seam. Empty if it is not there.
inline spz::Bytes on_disk(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return spz::Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

inline std::string text_on_disk(const std::string& path) {
    const spz::Bytes data = on_disk(path);
    return std::string(data.begin(), data.end());
}

inline bool is_there(const std::string& path) {
    return std::filesystem::exists(std::filesystem::symlink_status(path));
}

// The names directly in a folder, sorted.
inline std::vector<std::string> names_in(const std::filesystem::path& folder) {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) names.push_back(entry.path().filename().string());
    std::sort(names.begin(), names.end());
    return names;
}

// Sets a file's modification time, in seconds since 1970 (the fake clock's wall time is in the same).
inline void set_modified(const std::string& path, std::int64_t seconds_since_1970) {
    const auto wanted = std::chrono::system_clock::time_point(std::chrono::seconds(seconds_since_1970));
    const auto file_time = std::filesystem::file_time_type::clock::now() +
                           std::chrono::duration_cast<std::filesystem::file_time_type::duration>(
                               wanted - std::chrono::system_clock::now());
    std::filesystem::last_write_time(path, file_time);
}

// Whether a line is in a text, as a whole line.
inline bool has_line(const std::string& text, const std::string& line) {
    std::size_t at = 0;
    while (at <= text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string found = text.substr(at, end - at);
        if (!found.empty() && found.back() == '\r') found.pop_back();
        if (found == line) return true;
        at = end + 1;
    }
    return false;
}

// Whether bytes are valid UTF-8 (SPEC-009): no stray or missing continuation byte, no overlong form, no
// surrogate, nothing above U+10FFFF.
inline bool is_utf8(const spz::Bytes& data) {
    std::size_t i = 0;
    while (i < data.size()) {
        const std::uint8_t lead = data[i];
        std::size_t more = 0;
        std::uint32_t code = 0;
        if (lead < 0x80) { ++i; continue; }
        if (lead >= 0xC2 && lead <= 0xDF) { more = 1; code = lead & 0x1Fu; }
        else if (lead >= 0xE0 && lead <= 0xEF) { more = 2; code = lead & 0x0Fu; }
        else if (lead >= 0xF0 && lead <= 0xF4) { more = 3; code = lead & 0x07u; }
        else return false;
        if (i + more >= data.size()) return false;
        for (std::size_t k = 1; k <= more; ++k) {
            if ((data[i + k] & 0xC0) != 0x80) return false;
            code = (code << 6) | (data[i + k] & 0x3Fu);
        }
        if (more == 2 && (code < 0x800 || (code >= 0xD800 && code <= 0xDFFF))) return false;
        if (more == 3 && (code < 0x10000 || code > 0x10FFFF)) return false;
        i += more + 1;
    }
    return true;
}

}  // namespace spz::test
