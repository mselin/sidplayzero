// SPDX-License-Identifier: GPL-3.0-or-later
// Importing other players' playlists (SPEC-175 to SPEC-178).
#include "spz/importers.h"

#include <cstdlib>
#include <unordered_map>
#include <utility>

#include "spz/paths.h"
#include "spz/playlist.h"

namespace spz {

namespace {

using lists_text::as_utf8;
using lists_text::ascii_lower;
using lists_text::split_lines;

bool starts_with(const std::string& s, const std::string& prefix) { return s.compare(0, prefix.size(), prefix) == 0; }

bool ends_with(const std::string& s, const std::string& end) {
    return s.size() >= end.size() && s.compare(s.size() - end.size(), end.size(), end) == 0;
}

bool all_digits(const std::string& s) {
    if (s.empty()) return false;
    for (const char c : s) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

// A subtune number from digits: 1 to 256, else 0.
int subtune_of(const std::string& digits) {
    if (!all_digits(digits) || digits.size() > 6) return 0;
    const long n = std::strtol(digits.c_str(), nullptr, 10);
    return n >= 1 && n <= 256 ? static_cast<int>(n) : 0;
}

std::string trim(const std::string& s) {
    std::size_t begin = 0, end = s.size();
    while (begin < end && (s[begin] == ' ' || s[begin] == '\t')) ++begin;
    while (end > begin && (s[end - 1] == ' ' || s[end - 1] == '\t')) --end;
    return s.substr(begin, end - begin);
}

// The text of a file as UTF-8 lines: UTF-8 if the whole file is valid UTF-8, else Latin-1.
std::vector<std::string> text_lines(const Bytes& file) {
    std::vector<std::string> lines = split_lines(file);
    bool utf8 = true;
    for (const std::string& line : lines) utf8 = utf8 && lists_text::is_valid_utf8(line);
    for (std::string& line : lines) line = utf8 ? as_utf8(line) : lists_text::latin1_to_utf8(line);
    // A byte-order mark at the start is not part of the first line.
    if (!lines.empty() && starts_with(lines[0], "\xEF\xBB\xBF")) lines[0] = lines[0].substr(3);
    return lines;
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string percent_decoded(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            const int high = hex_value(s[i + 1]), low = hex_value(s[i + 2]);
            if (high >= 0 && low >= 0) {
                out += static_cast<char>(high * 16 + low);
                i += 2;
                continue;
            }
        }
        out += s[i];
    }
    return lists_text::is_valid_utf8(out) ? out : lists_text::latin1_to_utf8(out);
}

// The value of "key=N" inside a line such as "#PHOSPHOR:song=8" or "#SIDTUNE:subtune=1 md5=...".
int number_after(const std::string& line, const std::string& key) {
    std::size_t at = 0;
    while ((at = line.find(key, at)) != std::string::npos) {
        const bool starts_word = at == 0 || line[at - 1] == ' ' || line[at - 1] == ':' || line[at - 1] == ',' ||
                                 line[at - 1] == ';' || line[at - 1] == '\t';
        if (starts_word) {
            std::size_t end = at + key.size();
            while (end < line.size() && line[end] >= '0' && line[end] <= '9') ++end;
            const int n = subtune_of(line.substr(at + key.size(), end - at - key.size()));
            if (n > 0) return n;
        }
        at += key.size();
    }
    return 0;
}

ImportRead too_large() {
    ImportRead read;
    read.message = "MSG_FILE_TOO_LARGE";
    return read;
}

// Adds an entry, or says the list is full (SPEC-204). False once the list is cut.
bool add_line(ImportRead* read, ImportLine line) {
    if (read->lines.size() >= kPlaylistEntryLimit) {
        read->message = "MSG_LIST_CUT";
        return false;
    }
    read->lines.push_back(std::move(line));
    return true;
}

ImportRead finished(ImportRead read) {
    if (read.lines.empty() && read.message.empty()) read.message = "MSG_IMPORT_NOTHING";
    return read;
}

// --- JSON, as much as JSidplay2's files need: a reader that never recurses past a fixed depth -------------

class JsonReader {
public:
    explicit JsonReader(const Bytes& file) : data_(file) {}

    // An array of objects; each object's top-level "filename", if a string, is a tune. False if the file
    // is anything else.
    bool read_files(std::vector<std::string>* names) {
        space();
        if (!eat('[')) return false;
        space();
        if (eat(']')) return end();
        for (;;) {
            space();
            if (!object(names)) return false;
            space();
            if (eat(',')) continue;
            if (eat(']')) return end();
            return false;
        }
    }

private:
    bool end() {
        space();
        return at_ == data_.size();
    }
    void space() {
        while (at_ < data_.size() && (data_[at_] == ' ' || data_[at_] == '\t' || data_[at_] == '\n' || data_[at_] == '\r')) ++at_;
    }
    bool eat(char c) {
        if (at_ < data_.size() && data_[at_] == static_cast<std::uint8_t>(c)) {
            ++at_;
            return true;
        }
        return false;
    }
    bool object(std::vector<std::string>* names) {
        if (!eat('{')) return false;
        space();
        if (eat('}')) return true;
        std::string filename;
        bool has_filename = false;
        for (;;) {
            space();
            std::string key;
            if (!string(&key)) return false;
            space();
            if (!eat(':')) return false;
            space();
            if (key == "filename" && at_ < data_.size() && data_[at_] == '"') {
                if (!string(&filename)) return false;
                has_filename = true;
            } else if (!skip_value(0)) {
                return false;
            }
            space();
            if (eat(',')) continue;
            if (eat('}')) break;
            return false;
        }
        if (has_filename && !filename.empty()) names->push_back(filename);
        return true;
    }
    static void put_utf8(std::uint32_t code, std::string* out) {
        if (code < 0x80) {
            *out += static_cast<char>(code);
        } else if (code < 0x800) {
            *out += static_cast<char>(0xC0 | (code >> 6));
            *out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            *out += static_cast<char>(0xE0 | (code >> 12));
            *out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            *out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            *out += static_cast<char>(0xF0 | (code >> 18));
            *out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            *out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            *out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }
    bool hex4(std::uint32_t* out) {
        if (at_ + 4 > data_.size()) return false;
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const int digit = hex_value(static_cast<char>(data_[at_ + static_cast<std::size_t>(i)]));
            if (digit < 0) return false;
            value = value * 16 + static_cast<std::uint32_t>(digit);
        }
        at_ += 4;
        *out = value;
        return true;
    }
    bool string(std::string* out) {
        if (!eat('"')) return false;
        out->clear();
        while (at_ < data_.size()) {
            const char c = static_cast<char>(data_[at_++]);
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return false;
            if (c != '\\') {
                *out += c;
                continue;
            }
            if (at_ >= data_.size()) return false;
            const char e = static_cast<char>(data_[at_++]);
            switch (e) {
                case '"': *out += '"'; break;
                case '\\': *out += '\\'; break;
                case '/': *out += '/'; break;
                case 'b': *out += '\b'; break;
                case 'f': *out += '\f'; break;
                case 'n': *out += '\n'; break;
                case 'r': *out += '\r'; break;
                case 't': *out += '\t'; break;
                case 'u': {
                    std::uint32_t code = 0;
                    if (!hex4(&code)) return false;
                    if (code >= 0xD800 && code <= 0xDBFF && at_ + 1 < data_.size() && data_[at_] == '\\' &&
                        data_[at_ + 1] == 'u') {
                        at_ += 2;
                        std::uint32_t low = 0;
                        if (!hex4(&low)) return false;
                        if (low >= 0xDC00 && low <= 0xDFFF) {
                            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                        } else {
                            code = 0xFFFD;
                        }
                    } else if (code >= 0xD800 && code <= 0xDFFF) {
                        code = 0xFFFD;
                    }
                    put_utf8(code, out);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }
    bool skip_value(int depth) {
        if (depth > 64 || at_ >= data_.size()) return false;
        const char c = static_cast<char>(data_[at_]);
        if (c == '"') {
            std::string ignored;
            return string(&ignored);
        }
        if (c == '{' || c == '[') {
            const char close = c == '{' ? '}' : ']';
            ++at_;
            space();
            if (eat(close)) return true;
            for (;;) {
                space();
                if (c == '{') {
                    std::string key;
                    if (!string(&key)) return false;
                    space();
                    if (!eat(':')) return false;
                    space();
                }
                if (!skip_value(depth + 1)) return false;
                space();
                if (eat(',')) continue;
                return eat(close);
            }
        }
        // A number, true, false or null: the characters such a value is made of.
        const std::size_t begin = at_;
        while (at_ < data_.size()) {
            const char d = static_cast<char>(data_[at_]);
            const bool part = (d >= '0' && d <= '9') || (d >= 'a' && d <= 'z') || d == '-' || d == '+' || d == '.' ||
                              d == 'E';
            if (!part) break;
            ++at_;
        }
        if (at_ == begin) return false;
        const std::string word(reinterpret_cast<const char*>(data_.data()) + begin, at_ - begin);
        if (word == "true" || word == "false" || word == "null") return true;
        char* end = nullptr;
        std::strtod(word.c_str(), &end);
        return end != nullptr && *end == '\0' && (word[0] == '-' || (word[0] >= '0' && word[0] <= '9'));
    }

    const Bytes& data_;
    std::size_t at_ = 0;
};

// Whether a name from a Sidplay/w file is a full path: from a root, or after a drive letter.
bool is_full_path(const std::string& s) {
    if (s.empty()) return false;
    if (s[0] == '/' || s[0] == '\\') return true;
    return s.size() >= 2 && s[1] == ':' && ((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z'));
}

std::string name_without_ending(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    const std::size_t dot = name.rfind('.');
    if (dot != std::string::npos && dot > 0) name = name.substr(0, dot);
    return name;
}

const char* const kEndings[] = {".m3u", ".m3u8", ".js2", ".json", ".spl"};

}  // namespace

ImportRead read_m3u(const Bytes& file) {
    if (file.size() > kPlaylistFileLimit) return too_large();
    ImportRead read;
    int sidtune = 0, phosphor = 0;
    bool named = false;
    for (const std::string& original : text_lines(file)) {
        if (original.empty()) continue;
        if (original[0] == '#') {
            if (starts_with(original, "#PLAYLIST:")) {
                if (!named) read.name = original.substr(10);
                named = true;
            } else if (starts_with(original, "#SIDTUNE:")) {
                sidtune = number_after(original, "subtune=");
            } else if (starts_with(original, "#PHOSPHOR:")) {
                phosphor = number_after(original, "song=");
            }
            continue;
        }
        std::string text = original;
        int brace = 0, query = 0, tune_file = 0;
        // A leading "{N}".
        if (text[0] == '{') {
            const std::size_t close = text.find('}');
            if (close != std::string::npos && all_digits(text.substr(1, close - 1))) {
                brace = subtune_of(text.substr(1, close - 1));
                text = text.substr(close + 1);
            }
        }
        const bool url = starts_with(text, "file://");
        if (url) text = text.substr(7);
        // An ending "?N".
        const std::size_t mark = text.rfind('?');
        if (mark != std::string::npos && all_digits(text.substr(mark + 1))) {
            query = subtune_of(text.substr(mark + 1));
            text = text.substr(0, mark);
        }
        if (url) text = percent_decoded(text);
        // An ending "/tune_NNN.sid" after a name ending in ".sid".
        const std::size_t slash = text.rfind('/');
        if (slash != std::string::npos && slash >= 4 && ascii_lower(text.substr(slash - 4, 4)) == ".sid") {
            const std::string last = text.substr(slash + 1);
            if (starts_with(last, "tune_") && ends_with(ascii_lower(last), ".sid") &&
                all_digits(last.substr(5, last.size() - 9))) {
                tune_file = subtune_of(last.substr(5, last.size() - 9));
                text = text.substr(0, slash);
            }
        }
        if (text.empty()) continue;
        ImportLine line;
        line.text = text;
        line.original = original;
        line.subtune = sidtune ? sidtune : phosphor ? phosphor : brace ? brace : query ? query : tune_file;
        sidtune = phosphor = 0;
        if (!add_line(&read, std::move(line))) break;
    }
    return finished(std::move(read));
}

ImportRead read_js2(const Bytes& file) {
    if (file.size() > kPlaylistFileLimit) return too_large();
    ImportRead read;
    for (const std::string& original : text_lines(file)) {
        std::string text = trim(original);
        if (starts_with(text, "<HVSC>")) text = text.substr(6);
        if (text.empty()) continue;
        ImportLine line;
        line.text = text;
        line.original = original;
        if (!add_line(&read, std::move(line))) break;
    }
    return finished(std::move(read));
}

ImportRead read_jsidplay2_json(const Bytes& file) {
    if (file.size() > kPlaylistFileLimit) return too_large();
    ImportRead read;
    std::vector<std::string> names;
    JsonReader reader(file);
    if (!reader.read_files(&names)) {
        read.message = "MSG_IMPORT_NOTHING";
        return read;
    }
    for (const std::string& name : names) {
        std::string text = lists_text::is_valid_utf8(name) ? name : lists_text::latin1_to_utf8(name);
        ImportLine line;
        line.text = text;
        line.original = text;
        if (!add_line(&read, std::move(line))) break;
    }
    return finished(std::move(read));
}

ImportRead read_spl(const Bytes& file) {
    if (file.size() > kPlaylistFileLimit) return too_large();
    ImportRead read;
    const std::vector<std::string> lines = text_lines(file);
    std::size_t first = 0;
    while (first < lines.size() && trim(lines[first]).empty()) ++first;
    if (first >= lines.size() || ascii_lower(trim(lines[first])) != "[sidplay/windows playlist]") {
        read.message = "MSG_IMPORT_NOTHING";
        return read;
    }
    struct Item {
        std::string file;
        int subtune = 0;
    };
    std::vector<Item> items;
    std::string base;
    bool in_section = false;   // format 1: inside a "[N]" section, whose Filename= is its entry's
    for (std::size_t i = first + 1; i < lines.size(); ++i) {
        const std::string line = trim(lines[i]);
        if (line.empty() || line[0] == ';') continue;
        if (line[0] == '[') {
            items.push_back(Item());
            in_section = true;
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = ascii_lower(trim(line.substr(0, equals)));
        const std::string value = trim(line.substr(equals + 1));
        if (key == "basedir") {
            base = value;
        } else if (key == "file") {   // format 2: a block begins
            if (items.size() >= kPlaylistEntryLimit + 1) break;
            Item item;
            item.file = value;
            items.push_back(item);
            in_section = false;
        } else if (key == "filename") {
            if (!in_section || items.empty()) items.push_back(Item());
            items.back().file = value;
        } else if (key == "subsong" || key == "subtune") {
            if (!items.empty()) items.back().subtune = subtune_of(value);
        }
        if (items.size() > kPlaylistEntryLimit + 1) break;
    }
    for (const Item& item : items) {
        if (item.file.empty()) continue;
        ImportLine line;
        line.original = item.file;
        line.text = item.file;
        if (!base.empty() && !is_full_path(item.file)) {
            const char last = base.back();
            const char separator = base.find('\\') != std::string::npos ? '\\' : '/';
            line.text = base + (last == '\\' || last == '/' ? std::string() : std::string(1, separator)) + item.file;
        }
        line.subtune = item.subtune;
        if (!add_line(&read, std::move(line))) break;
    }
    return finished(std::move(read));
}

ImportRead read_import(const std::string& file_name, const Bytes& file) {
    const std::string lower = ascii_lower(file_name);
    if (ends_with(lower, ".m3u") || ends_with(lower, ".m3u8")) return read_m3u(file);
#if SPZ_PART_IMPORT_MORE
    if (ends_with(lower, ".js2")) return read_js2(file);
    if (ends_with(lower, ".json")) return read_jsidplay2_json(file);
    if (ends_with(lower, ".spl")) return read_spl(file);
#endif
    ImportRead read;
    read.message = "MSG_IMPORT_NOTHING";
    return read;
}

// --- Matching a path to a tune (SPEC-178) --------------------------------------------------------------

// What the listing has, by path without regard to case and by file name: built when first needed, once
// for a whole file being imported.
struct Importer::Names {
    const Listing* listing = nullptr;
    bool built = false;
    std::unordered_map<std::string, std::string> by_lower_path;
    std::unordered_map<std::string, std::pair<int, std::string>> by_name;   // how many have it, and the first

    void build() {
        if (built) return;
        built = true;
        if (listing == nullptr || !listing->is_open()) return;
        const int count = listing->tune_count();
        const int step = 4096;
        for (int first = 0; first < count; first += step) {
            for (const ListedTune& tune : listing->tunes(first, step)) {
                by_lower_path.emplace(ascii_lower(tune.path), tune.path);
                const std::size_t slash = tune.path.rfind('/');
                const std::string name = tune.path.substr(slash + 1);
                auto found = by_name.find(name);
                if (found == by_name.end()) by_name.emplace(name, std::make_pair(1, tune.path));
                else ++found->second.first;
            }
        }
    }
};

Importer::Importer(Files* files, const std::string& root, const std::string& import_folder, const Listing* listing,
                   Playlists* playlists)
    : files_(files), root_(root), import_folder_(import_folder), listing_(listing), playlists_(playlists) {}

std::vector<std::string> Importer::files() const {
    std::vector<std::string> out;
    for (const std::string& folder : {import_folder_, root_}) {
        std::vector<FileInfo> found;
        if (files_->list(folder, &found) != FileStatus::Ok) continue;
        for (const FileInfo& info : found) {
            if (info.is_folder || info.is_special || info.name.empty() || info.name[0] == '.') continue;
            const std::string lower = ascii_lower(info.name);
            for (const char* ending : kEndings) {
                if (ends_with(lower, ending) && lower.size() > std::string(ending).size()) {
                    out.push_back(folder + "/" + info.name);
                    break;
                }
            }
        }
    }
    return out;
}

bool Importer::find_tune(const std::string& candidate, bool any_case, Names* names, std::string* path) const {
    if (!is_collection_path(candidate)) return false;
    const bool listed = listing_ != nullptr && listing_->is_open();
    if (!any_case) {
        ListedTune tune;
        FileInfo info;
        if ((listed && listing_->by_path(candidate, &tune)) ||
            (files_->info(root_ + candidate, &info) == FileStatus::Ok && !info.is_folder && !info.is_special)) {
            *path = candidate;
            return true;
        }
        return false;
    }
    if (listed) {
        names->build();
        const auto found = names->by_lower_path.find(ascii_lower(candidate));
        if (found != names->by_lower_path.end()) {
            *path = found->second;
            return true;
        }
    }
    // Under the root, one part at a time, each found without regard to case (its own spelling first).
    std::string real;
    std::size_t at = 1;
    while (at <= candidate.size()) {
        std::size_t end = candidate.find('/', at);
        const bool last = end == std::string::npos;
        if (last) end = candidate.size();
        const std::string part = candidate.substr(at, end - at);
        std::vector<FileInfo> found;
        if (files_->list(root_ + real, &found) != FileStatus::Ok) return false;
        const FileInfo* match = nullptr;
        for (const FileInfo& info : found) {
            if (info.is_special || info.is_folder == last) continue;
            if (info.name == part) {
                match = &info;
                break;
            }
            if (match == nullptr && ascii_lower(info.name) == ascii_lower(part)) match = &info;
        }
        if (match == nullptr) return false;
        real += "/" + match->name;
        at = end + 1;
    }
    *path = real;
    return true;
}

bool Importer::match_with(const std::string& text, const std::string& folder_of_file, Names* names,
                          std::string* path) const {
    std::string t = text;
    for (char& c : t) {
        if (c == '\\') c = '/';
    }
    for (const bool any_case : {false, true}) {
        const std::string seen = any_case ? ascii_lower(t) : t;
        std::vector<std::string> forms;
        // The part after "/C64Music/".
        const std::string marker = any_case ? "/c64music/" : "/C64Music/";
        const std::size_t at = seen.find(marker);
        if (at != std::string::npos) forms.push_back(t.substr(at + marker.size() - 1));
        // The path as it stands, and with a "/" in front.
        forms.push_back(t);
        if (t.empty() || t[0] != '/') forms.push_back("/" + t);
        // The path taken relative to the folder of the file being imported, if that lands under the root.
        const std::string full = !t.empty() && t[0] == '/' ? t : folder_of_file + "/" + t;
        const std::string root_slash = root_ + "/";
        const std::string full_seen = any_case ? ascii_lower(full) : full;
        const std::string root_seen = any_case ? ascii_lower(root_slash) : root_slash;
        if (full_seen.compare(0, root_seen.size(), root_seen) == 0) forms.push_back(full.substr(root_.size()));

        for (const std::string& form : forms) {
            if (find_tune(form, any_case, names, path)) return true;
        }
    }
    // The file's name alone, if exactly one tune in the listing has it.
    if (listing_ != nullptr && listing_->is_open()) {
        const std::size_t slash = t.rfind('/');
        const std::string name = slash == std::string::npos ? t : t.substr(slash + 1);
        names->build();
        const auto found = names->by_name.find(name);
        if (!name.empty() && found != names->by_name.end() && found->second.first == 1 &&
            is_collection_path(found->second.second)) {
            *path = found->second.second;
            return true;
        }
    }
    return false;
}

bool Importer::match(const std::string& text, const std::string& folder_of_file, std::string* path) const {
    Names names;
    names.listing = listing_;
    return match_with(text, folder_of_file, &names, path);
}

ImportReport Importer::import_file(const std::string& path) {
    ImportReport report;
    Bytes bytes;
    const FileStatus status = files_->read(path, kPlaylistFileLimit, &bytes);
    if (status == FileStatus::TooLarge) {
        report.message = "MSG_FILE_TOO_LARGE";
        return report;
    }
    if (status != FileStatus::Ok) {
        report.message = "MSG_IMPORT_NOTHING";
        return report;
    }
    const std::size_t slash = path.rfind('/');
    const std::string folder = slash == std::string::npos ? std::string(".") : path.substr(0, slash);
    const ImportRead read = read_import(path.substr(slash == std::string::npos ? 0 : slash + 1), bytes);
    report.message = read.message;
    if (read.message == "MSG_FILE_TOO_LARGE" || read.message == "MSG_IMPORT_NOTHING") return report;

    Names names;
    names.listing = listing_;
    std::vector<PlaylistEntry> entries;
    for (const ImportLine& line : read.lines) {
        PlaylistEntry entry;
        std::string found;
        if (match_with(line.text, folder, &names, &found)) {
            entry.entry.path = found;
            entry.entry.subtune = line.subtune;
        } else {
            entry.entry.path = line.original;   // a missing entry holding the file's own text
            report.not_found.push_back(line.original);
        }
        entries.push_back(entry);
    }

    // The name: the file's "#PLAYLIST:" line, or its name without the ending; no control characters.
    std::string wanted;
    for (const char c : read.name.empty() ? name_without_ending(path) : read.name) {
        const unsigned char byte = static_cast<unsigned char>(c);
        if (byte >= 0x20 && byte != 0x7F) wanted += c;
    }
    if (!lists_text::is_valid_utf8(wanted)) wanted = lists_text::latin1_to_utf8(wanted);
    if (trim(wanted).empty()) wanted = name_without_ending(path);
    if (trim(wanted).empty()) wanted = "Imported";
    for (int n = 1; n < 100000; ++n) {
        const std::string added = n == 1 ? std::string() : " " + std::to_string(n);
        const std::string name = lists_text::utf8_prefix(wanted, kPlaylistNameLimit - added.size()) + added;
        const ListsResult made = playlists_->create(name, entries);
        if (made.ok) {
            report.playlist = name;
            report.entries = static_cast<int>(entries.size());
            return report;
        }
        if (made.message != "MSG_NAME_TAKEN") {
            if (report.message.empty()) report.message = made.message;
            return report;
        }
    }
    return report;
}

}  // namespace spz
