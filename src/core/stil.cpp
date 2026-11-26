// SPDX-License-Identifier: GPL-3.0-or-later
// Notes from STIL (SPEC-097): the entry DOCUMENTS/STIL.txt has for a tune, read with the limits of SPEC-204.
#include "spz/stil.h"

#include <cstdint>
#include <vector>

#include "spz/types.h"

namespace spz {

const char* const kNoNotes = "No notes for this tune";

namespace {

const std::size_t kLineLimit = 4096;    // SPEC-204: a longer line is cut there

// Latin-1 to UTF-8: every byte is the code point of the same number.
std::string latin1_to_utf8(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        const unsigned char byte = static_cast<unsigned char>(c);
        if (byte < 0x80) {
            out += c;
        } else {
            out += static_cast<char>(0xC0 | (byte >> 6));
            out += static_cast<char>(0x80 | (byte & 0x3F));
        }
    }
    return out;
}

// The lines of the file, one at a time: a line ends at LF, with a CR before it dropped; a zero byte ends
// its line, and a line is cut at 4,096 bytes, the rest of it dropped in both cases.
class Lines {
public:
    explicit Lines(const Bytes& bytes) : bytes_(bytes) {}

    bool next(std::string* line) {
        if (at_ >= bytes_.size()) return false;
        std::size_t end = at_;
        while (end < bytes_.size() && bytes_[end] != '\n') ++end;
        std::size_t stop = at_;
        while (stop < end && bytes_[stop] != 0 && stop - at_ < kLineLimit) ++stop;
        if (stop == end && stop > at_ && bytes_[stop - 1] == '\r') --stop;
        line->assign(reinterpret_cast<const char*>(bytes_.data()) + at_, stop - at_);
        at_ = end + 1;
        return true;
    }

private:
    const Bytes& bytes_;
    std::size_t at_ = 0;
};

// The subtune of a line of the form "(#n)", or 0 if the line is not of that form.
int subtune_line(const std::string& line) {
    std::size_t end = line.size();
    while (end > 0 && (line[end - 1] == ' ' || line[end - 1] == '\t')) --end;
    if (end < 4 || line[0] != '(' || line[1] != '#' || line[end - 1] != ')') return 0;
    int number = 0;
    for (std::size_t i = 2; i + 1 < end; ++i) {
        if (line[i] < '0' || line[i] > '9') return 0;
        number = number * 10 + (line[i] - '0');
        if (number > 100000) return 0;
    }
    return number;
}

StilNotes none(const std::string& notice = "") {
    StilNotes notes;
    notes.text = kNoNotes;
    notes.notice = notice;
    return notes;
}

}  // namespace

StilNotes stil_notes(Files* files, const std::string& stil_file, const std::string& path, int subtune) {
    if (files == nullptr) return none();
    Bytes bytes;
    const FileStatus status = files->read(stil_file, kStilSizeLimit, &bytes);
    if (status == FileStatus::TooLarge) return none("MSG_FILE_TOO_LARGE");
    if (status != FileStatus::Ok || path.empty()) return none();

    Lines lines(bytes);
    std::string line;
    bool inside = false;        // in an entry, between its path's line and the empty line after it
    bool ours = false;          // and that entry is the tune's
    int part = 0;               // the subtune the lines are about now; 0 for the file
    std::vector<std::string> kept;
    bool found = false;
    while (lines.next(&line)) {
        if (!inside) {
            if (!line.empty() && line[0] == '/') {
                inside = true;
                ours = line == path;
                part = 0;
                if (ours) found = true;
            }
            continue;
        }
        if (line.empty()) {
            inside = false;
            if (ours) break;
            continue;
        }
        if (!ours) continue;
        const int number = subtune_line(line);
        if (number > 0) {
            part = number;
            continue;
        }
        if (part == 0 || part == subtune) kept.push_back(line);
    }
    if (!found || kept.empty()) return none();

    StilNotes notes;
    notes.found = true;
    std::string text;
    for (std::size_t i = 0; i < kept.size(); ++i) {
        if (i) text += '\n';
        text += kept[i];
    }
    notes.text = latin1_to_utf8(text);
    return notes;
}

}  // namespace spz
