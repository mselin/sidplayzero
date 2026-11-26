// SPDX-License-Identifier: GPL-3.0-or-later
// Private to settings_file.cpp and state_file.cpp: the form the two files share (SPEC-130, SPEC-131,
// SPEC-133), read into lines and written back with the lines the app does not understand kept as they were.
#pragma once

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "spz/limits.h"
#include "spz/text.h"
#include "spz/types.h"

namespace spz::keep {

// Whether a value can be written on a line of the file and read back as it is: valid UTF-8, no control
// character, and no space or tab at either end (those are not part of a value).
inline bool fits_a_line(const std::string& value) {
    for (const char c : value) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte == 0x7F) return false;
    }
    if (!value.empty() && (value.front() == ' ' || value.back() == ' ')) return false;
    return text::is_valid_utf8(value);
}

using text::trim;

// One line of the file as it is kept for writing again.
struct KeptLine {
    std::string key;   // a known key, written with the value in force; "" for a line kept as it was
    std::string raw;   // the line as it was found
};

// What reading a file gives, before its values are looked at.
struct Lines {
    bool used = true;     // false: larger than the limit, or not valid UTF-8; nothing of it is used
    bool fault = false;   // a line that is not empty, a comment or key = value; a known key given twice
    std::vector<std::pair<std::string, std::string>> values;   // each known key once, with its last value,
                                                               // in the order of its first line
    std::vector<KeptLine> kept;   // what the file is written back from: the comments, the empty lines and
                                  // the lines of unknown keys as they were, each known key at its first line
};

// Reads the shared form: `key = value` lines, empty lines and lines starting with `#`. `known` says which
// keys are the app's. Never fails, whatever the bytes; takes time in proportion to them.
inline Lines read_lines(const Bytes& file, FileKind kind, const std::set<std::string>& known) {
    Lines result;
    if (file.size() > size_limit(kind) ||
        !text::is_valid_utf8(reinterpret_cast<const char*>(file.data()), file.size())) {
        result.used = false;
        result.fault = true;
        return result;
    }
    std::set<std::string> seen;
    for (const std::string& raw : split_lines(file)) {
        const std::string line = trim(raw);
        if (line.empty() || line[0] == '#') {
            result.kept.push_back({"", raw});
            continue;
        }
        const std::size_t equals = line.find('=');
        const std::string key = equals == std::string::npos ? "" : trim(line.substr(0, equals));
        if (key.empty()) {
            result.fault = true;    // not key = value: dropped, so that what the app writes has no fault
            continue;
        }
        const std::string value = trim(line.substr(equals + 1));
        if (known.count(key) == 0) {
            result.kept.push_back({"", raw});
            continue;
        }
        if (!seen.insert(key).second) {
            result.fault = true;    // a known key given twice: the last line wins (FIND-021)
            for (auto& pair : result.values) {
                if (pair.first == key) pair.second = value;
            }
            continue;
        }
        result.values.emplace_back(key, value);
        result.kept.push_back({key, raw});
    }
    return result;
}

}  // namespace spz::keep
