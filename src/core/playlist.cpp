// SPDX-License-Identifier: GPL-3.0-or-later
// A playlist's file (SPEC-170): reading and writing its text.
#include "spz/playlist.h"

#include <cstdlib>

namespace spz {

const char* const kFavouritesName = "Favourites";

namespace lists_text {

bool is_valid_utf8(const std::string& s) {
    std::size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        std::size_t more = 0;
        char32_t point = c;
        if (c < 0x80) {
            more = 0;
        } else if (c >= 0xC2 && c <= 0xDF) {
            more = 1;
            point = c & 0x1F;
        } else if (c >= 0xE0 && c <= 0xEF) {
            more = 2;
            point = c & 0x0F;
        } else if (c >= 0xF0 && c <= 0xF4) {
            more = 3;
            point = c & 0x07;
        } else {
            return false;
        }
        if (i + more >= s.size() && more > 0) return false;
        for (std::size_t k = 1; k <= more; ++k) {
            const unsigned char next = static_cast<unsigned char>(s[i + k]);
            if ((next & 0xC0) != 0x80) return false;
            point = (point << 6) | (next & 0x3F);
        }
        if ((more == 2 && point < 0x800) || (more == 3 && (point < 0x10000 || point > 0x10FFFF))) return false;
        if (point >= 0xD800 && point <= 0xDFFF) return false;
        i += more + 1;
    }
    return true;
}

std::string latin1_to_utf8(const std::string& s) {
    std::string out;
    out.reserve(s.size() + s.size() / 4);
    for (const char c : s) {
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

std::size_t utf8_length(const std::string& s) {
    std::size_t n = 0;
    for (const char c : s) {
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
    }
    return n;
}

std::string utf8_prefix(const std::string& s, std::size_t characters) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) {
            if (n == characters) return s.substr(0, i);
            ++n;
        }
    }
    return s;
}

std::string ascii_lower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

bool same_ignoring_case(const std::string& a, const std::string& b) {
    return a.size() == b.size() && ascii_lower(a) == ascii_lower(b);
}

std::vector<std::string> split_lines(const Bytes& file) {
    std::vector<std::string> lines;
    std::string line;
    bool ended = false;   // a zero byte or the length limit has ended the line: the rest of it is dropped
    bool any = false;
    for (std::size_t i = 0; i < file.size(); ++i) {
        const char c = static_cast<char>(file[i]);
        if (c == '\n' || c == '\r') {
            lines.push_back(line);
            line.clear();
            ended = false;
            any = false;
            if (c == '\r' && i + 1 < file.size() && file[i + 1] == '\n') ++i;
            continue;
        }
        any = true;
        if (ended) continue;
        if (c == '\0') {
            ended = true;
            continue;
        }
        if (line.size() >= kPlaylistLineLimit) {
            ended = true;
            continue;
        }
        line += c;
    }
    if (any) lines.push_back(line);
    return lines;
}

std::string as_utf8(std::string line) {
    if (is_valid_utf8(line)) return line;
    // A line cut at the limit may end inside a character: drop that part first.
    for (std::size_t drop = 1; drop <= 3 && drop < line.size(); ++drop) {
        const std::string shorter = line.substr(0, line.size() - drop);
        if (is_valid_utf8(shorter)) {
            const unsigned char first = static_cast<unsigned char>(line[line.size() - drop]);
            if (first >= 0xC2) return shorter;
            break;
        }
    }
    return latin1_to_utf8(line);
}

}  // namespace lists_text

namespace {

using namespace lists_text;

bool starts_with(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

bool is_md5(const std::string& s) {
    if (s.size() != 32) return false;
    for (const char c : s) {
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!hex) return false;
    }
    return true;
}

// A whole number of at most nine digits; -1 if the text is not one.
long small_number(const std::string& s) {
    if (s.empty() || s.size() > 9) return -1;
    for (const char c : s) {
        if (c < '0' || c > '9') return -1;
    }
    return std::strtol(s.c_str(), nullptr, 10);
}

// What is waiting for the next tune's line.
struct Pending {
    std::vector<std::string> raw;     // every "#" line as it was, for the end of the file
    std::vector<std::string> other;   // the lines the app does not know
    bool has_extinf = false;
    std::string author, title;
    int length_s = -1;
    int subtune = 0;
    std::string md5;
};

void read_extinf(const std::string& rest, Pending* p) {
    p->has_extinf = true;
    const std::size_t comma = rest.find(',');
    const std::string length = comma == std::string::npos ? rest : rest.substr(0, comma);
    const long seconds = small_number(length);
    p->length_s = seconds < 0 ? -1 : static_cast<int>(seconds);
    p->author.clear();
    p->title.clear();
    if (comma == std::string::npos) return;
    const std::string text = rest.substr(comma + 1);
    const std::size_t dash = text.find(" - ");
    if (dash == std::string::npos) {
        p->title = text;
    } else {
        p->author = text.substr(0, dash);
        p->title = text.substr(dash + 3);
    }
}

void read_sidtune(const std::string& rest, Pending* p) {
    std::size_t at = 0;
    while (at < rest.size()) {
        std::size_t end = rest.find(' ', at);
        if (end == std::string::npos) end = rest.size();
        const std::string word = rest.substr(at, end - at);
        if (starts_with(word, "subtune=")) {
            const long n = small_number(word.substr(8));
            if (n >= 1 && n <= 256) p->subtune = static_cast<int>(n);
        } else if (starts_with(word, "md5=")) {
            const std::string md5 = word.substr(4);
            if (is_md5(md5)) p->md5 = ascii_lower(md5);
        }
        at = end + 1;
    }
}

// Text for the "#EXTINF:" line: no control character may get into the file (SPEC-009).
std::string clean(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        const unsigned char byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte == 0x7F) c = ' ';
    }
    return out;
}

void put(Bytes* out, const std::string& line) {
    out->insert(out->end(), line.begin(), line.end());
    out->push_back('\n');
}

}  // namespace

PlaylistRead read_playlist(const Bytes& file) {
    PlaylistRead result;
    if (file.size() > kPlaylistFileLimit) {
        result.message = "MSG_FILE_TOO_LARGE";
        return result;
    }
    Playlist& list = result.playlist;
    Pending pending;
    bool named = false;
    for (const std::string& raw_line : split_lines(file)) {
        if (raw_line.empty()) continue;
        const std::string line = as_utf8(raw_line);
        if (line[0] == '#') {
            if (line == "#EXTM3U") continue;
            if (starts_with(line, "#PLAYLIST:")) {
                if (!named) list.name = line.substr(10);
                named = true;
                continue;
            }
            pending.raw.push_back(line);
            if (starts_with(line, "#EXTINF:")) {
                read_extinf(line.substr(8), &pending);
            } else if (starts_with(line, "#SIDTUNE:")) {
                read_sidtune(line.substr(9), &pending);
            } else {
                pending.other.push_back(line);
            }
            continue;
        }
        if (list.entries.size() >= kPlaylistEntryLimit) {
            result.message = "MSG_LIST_CUT";
            pending = Pending();
            break;
        }
        PlaylistEntry entry;
        entry.entry.path = line;
        entry.entry.subtune = pending.subtune;
        entry.entry.md5 = pending.md5;
        if (pending.has_extinf) {
            entry.author = pending.author;
            entry.title = pending.title;
            entry.length_s = pending.length_s;
        }
        entry.other_lines = std::move(pending.other);
        list.entries.push_back(std::move(entry));
        pending = Pending();
    }
    list.last_lines = pending.raw;
    return result;
}

Bytes write_playlist(const Playlist& playlist) {
    Bytes out;
    put(&out, "#EXTM3U");
    put(&out, "#PLAYLIST:" + clean(playlist.name));
    for (const PlaylistEntry& entry : playlist.entries) {
        for (const std::string& line : entry.other_lines) put(&out, line);
        if (!entry.title.empty() || entry.length_s >= 0) {
            std::string text = clean(entry.title);
            if (!entry.author.empty()) text = clean(entry.author) + " - " + text;
            put(&out, "#EXTINF:" + std::to_string(entry.length_s < 0 ? -1 : entry.length_s) + "," + text);
        }
        if (entry.entry.subtune > 0 || !entry.entry.md5.empty()) {
            std::string line = "#SIDTUNE:";
            if (entry.entry.subtune > 0) line += "subtune=" + std::to_string(entry.entry.subtune);
            if (!entry.entry.md5.empty()) {
                if (entry.entry.subtune > 0) line += " ";
                line += "md5=" + entry.entry.md5;
            }
            put(&out, line);
        }
        put(&out, entry.entry.path);
    }
    for (const std::string& line : playlist.last_lines) put(&out, line);
    return out;
}

std::string playlist_file_name(const std::string& name) {
    // One "_" for each character that is not kept, however many bytes it takes in UTF-8.
    std::string out;
    out.reserve(name.size() + 5);
    for (const char c : name) {
        if ((static_cast<unsigned char>(c) & 0xC0) == 0x80) continue;
        const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' ||
                          c == '-' || c == '_';
        out += keep ? c : '_';
    }
    return out + ".m3u8";
}

bool is_playlist_name(const std::string& name) {
    if (name.empty() || !is_valid_utf8(name)) return false;
    const std::size_t length = utf8_length(name);
    if (length < 1 || length > kPlaylistNameLimit) return false;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (c < 0x20 || c == 0x7F) return false;
        // The C1 control characters, U+0080 to U+009F.
        if (c == 0xC2 && i + 1 < name.size()) {
            const unsigned char next = static_cast<unsigned char>(name[i + 1]);
            if (next >= 0x80 && next <= 0x9F) return false;
        }
    }
    return true;
}

}  // namespace spz
