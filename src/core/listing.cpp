// SPDX-License-Identifier: GPL-3.0-or-later
// The listing (SPEC-142): the app's own reader of HVSC's song length database, and listing.bin, the file
// it writes for the quick worker to map and ask.
//
// listing.bin, all numbers little-endian:
//   header, 64 bytes:
//      0  "SPZLIST1"
//      8  u64  the database's size          16  i64  its modification time, seconds
//     24  i32  and nanoseconds              28  u32  T, the number of tunes
//     32  u32  F, folders, the root first   36  u32  L, lengths
//     40  u32  C, entries of the child table  44  u32  S, bytes of text
//     48  u64  the file's whole size        56  u64  0
//   T tune records, 32 bytes each, in the order of SPEC-042:
//     u32 path (offset in the text), u16 its length, u16 where its name starts in it,
//     u32 first length, u32 number of lengths, 16 bytes of MD5
//   F folder records, 32 bytes each, the root first and then in the order of SPEC-042 (each folder before
//   what it holds); "where each folder starts":
//     u32 path ("" for the root, else "/A/B"), u32 its length, u32 first child, u32 number of children,
//     u32 first tune, u32 number of tunes, 8 bytes of 0
//   C u32: the folders' child folders, each folder's together and in order
//   L u32: the lengths in milliseconds
//   T u32: the tunes by path, in plain byte order
//   F u32: the folders by path, in plain byte order
//   T u32: the tunes by MD5, and in listing order for one MD5
//   S bytes of text
#include "spz/listing.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <unordered_map>

#include "spz/paths.h"
#include "spz/safe_file.h"

namespace spz {

namespace {

const char kMagic[8] = {'S', 'P', 'Z', 'L', 'I', 'S', 'T', '1'};
const std::size_t kHeaderSize = 64;
const std::size_t kTuneRecord = 32;
const std::size_t kFolderRecord = 32;
const std::size_t kLineCut = 4096;        // SPEC-204

// --- reading the database -------------------------------------------------------------------------------

struct Parsed {
    std::string path;
    std::uint8_t md5[16];
    std::vector<std::uint32_t> lengths;
};

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }

// m:ss or m:ss.SSS, at most six digits of minutes.
bool parse_length(const char* p, std::size_t n, std::uint32_t* out) {
    std::size_t i = 0;
    std::uint64_t minutes = 0;
    while (i < n && is_digit(p[i])) {
        if (i >= 6) return false;
        minutes = minutes * 10 + static_cast<std::uint64_t>(p[i] - '0');
        ++i;
    }
    if (i == 0 || i >= n || p[i] != ':') return false;
    ++i;
    if (n - i < 2 || !is_digit(p[i]) || !is_digit(p[i + 1])) return false;
    const int seconds = (p[i] - '0') * 10 + (p[i + 1] - '0');
    if (seconds > 59) return false;
    i += 2;
    std::uint64_t ms = (minutes * 60 + static_cast<std::uint64_t>(seconds)) * 1000;
    if (i < n) {
        if (n - i != 4 || p[i] != '.' || !is_digit(p[i + 1]) || !is_digit(p[i + 2]) || !is_digit(p[i + 3])) return false;
        ms += static_cast<std::uint64_t>((p[i + 1] - '0') * 100 + (p[i + 2] - '0') * 10 + (p[i + 3] - '0'));
    }
    if (ms > 0xFFFFFFFFu) return false;
    *out = static_cast<std::uint32_t>(ms);
    return true;
}

// "MD5=length length ...": 32 hexadecimal digits, then at least one length, separated by spaces.
bool parse_md5_line(const std::string& line, Parsed* out) {
    if (line.size() < 34 || line[32] != '=') return false;
    for (int i = 0; i < 16; ++i) {
        const int high = hex_value(line[2 * i]), low = hex_value(line[2 * i + 1]);
        if (high < 0 || low < 0) return false;
        out->md5[i] = static_cast<std::uint8_t>(high * 16 + low);
    }
    out->lengths.clear();
    std::size_t i = 33;
    while (i < line.size()) {
        if (line[i] == ' ' || line[i] == '\t') {
            ++i;
            continue;
        }
        std::size_t end = i;
        while (end < line.size() && line[end] != ' ' && line[end] != '\t') ++end;
        std::uint32_t ms;
        if (!parse_length(line.data() + i, end - i, &ms)) return false;
        out->lengths.push_back(ms);
        i = end;
    }
    return !out->lengths.empty();
}

// --- the order of SPEC-042 (FIND-018) -----------------------------------------------------------------------

char folded(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

// <0, 0 or >0 for a[ab, ae) against b[bb, be): ASCII letters as lower case; a tie by plain bytes.
int compare_names(const std::string& a, std::size_t ab, std::size_t ae, const std::string& b, std::size_t bb, std::size_t be) {
    const std::size_t an = ae - ab, bn = be - bb, n = std::min(an, bn);
    for (std::size_t i = 0; i < n; ++i) {
        const unsigned char x = static_cast<unsigned char>(folded(a[ab + i]));
        const unsigned char y = static_cast<unsigned char>(folded(b[bb + i]));
        if (x != y) return x < y ? -1 : 1;
    }
    if (an != bn) return an < bn ? -1 : 1;
    for (std::size_t i = 0; i < n; ++i) {
        const unsigned char x = static_cast<unsigned char>(a[ab + i]), y = static_cast<unsigned char>(b[bb + i]);
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

// Whether the tune at path `a` comes before the tune at `b` in the listing: folder by folder, a folder's
// folders before its tunes, each by name. Both are HVSC paths.
bool path_before(const std::string& a, const std::string& b) {
    std::size_t i = 1, j = 1;
    for (;;) {
        std::size_t ea = a.find('/', i), eb = b.find('/', j);
        const bool folder_a = ea != std::string::npos, folder_b = eb != std::string::npos;
        if (!folder_a) ea = a.size();
        if (!folder_b) eb = b.size();
        if (folder_a != folder_b) return folder_a;
        const int c = compare_names(a, i, ea, b, j, eb);
        if (c != 0) return c < 0;
        if (!folder_a) return false;
        i = ea + 1;
        j = eb + 1;
    }
}

// --- writing the file ------------------------------------------------------------------------------------

void put_u16(Bytes* out, std::size_t at, std::uint32_t v) {
    (*out)[at] = static_cast<std::uint8_t>(v);
    (*out)[at + 1] = static_cast<std::uint8_t>(v >> 8);
}
void put_u32(Bytes* out, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) (*out)[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v >> (8 * i));
}
void put_u64(Bytes* out, std::size_t at, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) (*out)[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v >> (8 * i));
}

std::uint32_t get_u16(const std::uint8_t* p) { return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8); }
std::uint32_t get_u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}
std::uint64_t get_u64(const std::uint8_t* p) {
    std::uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

struct Folder {
    std::string path;                 // "" for the root
    std::vector<std::uint32_t> children;
    std::uint32_t first_tune = 0, tune_count = 0;
};

// The folder a tune or folder path is in: "/A/B.sid" gives "/A", "/A" gives "".
std::string parent_of(const std::string& path) { return path.substr(0, path.rfind('/')); }

std::uint32_t folder_id(const std::string& path, std::vector<Folder>* folders, std::unordered_map<std::string, std::uint32_t>* ids) {
    const auto found = ids->find(path);
    if (found != ids->end()) return found->second;
    const std::uint32_t parent = folder_id(parent_of(path), folders, ids);
    const std::uint32_t id = static_cast<std::uint32_t>(folders->size());
    folders->push_back(Folder{path, {}, 0, 0});
    (*folders)[parent].children.push_back(id);
    (*ids)[path] = id;
    return id;
}

Bytes make_file(const std::vector<Parsed>& tunes, const FileInfo& database) {
    // The folders, the root first, each where its first tune makes it.
    std::vector<Folder> folders{Folder{"", {}, 0, 0}};
    std::unordered_map<std::string, std::uint32_t> ids{{"", 0}};
    for (std::uint32_t t = 0; t < tunes.size(); ++t) {
        Folder& folder = folders[folder_id(parent_of(tunes[t].path), &folders, &ids)];
        if (folder.tune_count == 0) folder.first_tune = t;
        ++folder.tune_count;   // a folder's tunes come together: its folders, with all they hold, come first
    }

    std::size_t length_count = 0, child_count = 0, text_size = 0;
    for (const Parsed& tune : tunes) {
        length_count += tune.lengths.size();
        text_size += tune.path.size();
    }
    for (const Folder& folder : folders) {
        child_count += folder.children.size();
        text_size += folder.path.size();
    }
    const std::size_t T = tunes.size(), F = folders.size();
    const std::size_t tunes_at = kHeaderSize, folders_at = tunes_at + T * kTuneRecord,
                      children_at = folders_at + F * kFolderRecord, lengths_at = children_at + 4 * child_count,
                      by_path_at = lengths_at + 4 * length_count, folders_by_path_at = by_path_at + 4 * T,
                      by_md5_at = folders_by_path_at + 4 * F, text_at = by_md5_at + 4 * T, total = text_at + text_size;

    Bytes out(total, 0);
    std::memcpy(out.data(), kMagic, sizeof kMagic);
    put_u64(&out, 8, database.size);
    put_u64(&out, 16, static_cast<std::uint64_t>(database.modified_s));
    put_u32(&out, 24, static_cast<std::uint32_t>(database.modified_ns));
    put_u32(&out, 28, static_cast<std::uint32_t>(T));
    put_u32(&out, 32, static_cast<std::uint32_t>(F));
    put_u32(&out, 36, static_cast<std::uint32_t>(length_count));
    put_u32(&out, 40, static_cast<std::uint32_t>(child_count));
    put_u32(&out, 44, static_cast<std::uint32_t>(text_size));
    put_u64(&out, 48, total);

    std::size_t text = 0, length = 0;
    for (std::size_t t = 0; t < T; ++t) {
        const Parsed& tune = tunes[t];
        const std::size_t at = tunes_at + t * kTuneRecord;
        put_u32(&out, at, static_cast<std::uint32_t>(text));
        put_u16(&out, at + 4, static_cast<std::uint32_t>(tune.path.size()));
        put_u16(&out, at + 6, static_cast<std::uint32_t>(tune.path.rfind('/') + 1));
        put_u32(&out, at + 8, static_cast<std::uint32_t>(length));
        put_u32(&out, at + 12, static_cast<std::uint32_t>(tune.lengths.size()));
        std::memcpy(out.data() + at + 16, tune.md5, 16);
        std::memcpy(out.data() + text_at + text, tune.path.data(), tune.path.size());
        text += tune.path.size();
        for (const std::uint32_t ms : tune.lengths) put_u32(&out, lengths_at + 4 * length++, ms);
    }
    std::size_t child = 0;
    for (std::size_t f = 0; f < F; ++f) {
        const Folder& folder = folders[f];
        const std::size_t at = folders_at + f * kFolderRecord;
        put_u32(&out, at, static_cast<std::uint32_t>(text));
        put_u32(&out, at + 4, static_cast<std::uint32_t>(folder.path.size()));
        put_u32(&out, at + 8, static_cast<std::uint32_t>(child));
        put_u32(&out, at + 12, static_cast<std::uint32_t>(folder.children.size()));
        put_u32(&out, at + 16, folder.first_tune);
        put_u32(&out, at + 20, folder.tune_count);
        std::memcpy(out.data() + text_at + text, folder.path.data(), folder.path.size());
        text += folder.path.size();
        for (const std::uint32_t id : folder.children) put_u32(&out, children_at + 4 * child++, id);
    }

    std::vector<std::uint32_t> order(T);
    for (std::uint32_t t = 0; t < T; ++t) order[t] = t;
    std::sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) { return tunes[a].path < tunes[b].path; });
    for (std::size_t i = 0; i < T; ++i) put_u32(&out, by_path_at + 4 * i, order[i]);
    std::sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) {
        const int c = std::memcmp(tunes[a].md5, tunes[b].md5, 16);
        return c != 0 ? c < 0 : a < b;
    });
    for (std::size_t i = 0; i < T; ++i) put_u32(&out, by_md5_at + 4 * i, order[i]);

    std::vector<std::uint32_t> folder_order(F);
    for (std::uint32_t f = 0; f < F; ++f) folder_order[f] = f;
    std::sort(folder_order.begin(), folder_order.end(),
              [&](std::uint32_t a, std::uint32_t b) { return folders[a].path < folders[b].path; });
    for (std::size_t i = 0; i < F; ++i) put_u32(&out, folders_by_path_at + 4 * i, folder_order[i]);
    return out;
}

}  // namespace

// --- the mapped file ---------------------------------------------------------------------------------------

struct Listing::Map {
    MappedFile file;
    std::uint32_t tunes = 0, folders = 0, lengths = 0, children = 0, text_size = 0;
    std::size_t tunes_at = 0, folders_at = 0, children_at = 0, lengths_at = 0, by_path_at = 0, folders_by_path_at = 0,
                by_md5_at = 0, text_at = 0;

    const std::uint8_t* at(std::size_t offset) const { return file.data() + offset; }

    // The text at [offset, offset + size), if it is inside the text.
    bool text(std::uint32_t offset, std::uint32_t size, const char** out) const {
        if (static_cast<std::uint64_t>(offset) + size > text_size) return false;
        *out = reinterpret_cast<const char*>(at(text_at + offset));
        return true;
    }
    bool tune_path(std::uint32_t t, std::string* out) const {
        if (t >= tunes) return false;
        const std::uint8_t* record = at(tunes_at + t * kTuneRecord);
        const char* p;
        if (!text(get_u32(record), get_u16(record + 4), &p)) return false;
        out->assign(p, get_u16(record + 4));
        return true;
    }
    bool tune(std::uint32_t t, ListedTune* out) const {
        if (t >= tunes) return false;
        const std::uint8_t* record = at(tunes_at + t * kTuneRecord);
        const std::uint32_t first = get_u32(record + 8), count = get_u32(record + 12);
        if (static_cast<std::uint64_t>(first) + count > lengths) return false;
        ListedTune tune;
        if (!tune_path(t, &tune.path)) return false;
        static const char kHex[] = "0123456789abcdef";
        for (int i = 0; i < 16; ++i) {
            tune.md5 += kHex[record[16 + i] >> 4];
            tune.md5 += kHex[record[16 + i] & 0xF];
        }
        tune.lengths.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) tune.lengths.push_back(get_u32(at(lengths_at + 4 * (static_cast<std::size_t>(first) + i))));
        *out = std::move(tune);
        return true;
    }
    bool tune_name(std::uint32_t t, std::string* out) const {
        if (!tune_path(t, out)) return false;
        const std::uint32_t from = get_u16(at(tunes_at + t * kTuneRecord + 6));
        if (from > out->size()) return false;
        out->erase(0, from);
        return true;
    }
    bool folder_path(std::uint32_t f, std::string* out) const {
        if (f >= folders) return false;
        const std::uint8_t* record = at(folders_at + f * kFolderRecord);
        const char* p;
        if (!text(get_u32(record), get_u32(record + 4), &p)) return false;
        out->assign(p, get_u32(record + 4));
        return true;
    }
    // Binary search of a table of ids by a path; `path_of` gives an id's path. UINT32_MAX if not there.
    template <typename PathOf>
    std::uint32_t find(std::size_t table_at, std::uint32_t count, const std::string& path, PathOf path_of) const {
        std::uint32_t low = 0, high = count;
        std::string probe;
        while (low < high) {
            const std::uint32_t middle = low + (high - low) / 2;
            const std::uint32_t id = get_u32(at(table_at + 4 * static_cast<std::size_t>(middle)));
            if (!path_of(id, &probe)) return UINT32_MAX;   // damaged
            if (probe < path) low = middle + 1;
            else high = middle;
        }
        if (low >= count) return UINT32_MAX;
        const std::uint32_t id = get_u32(at(table_at + 4 * static_cast<std::size_t>(low)));
        return path_of(id, &probe) && probe == path ? id : UINT32_MAX;
    }
};

ListingBuild Listing::build(Files* files, const std::string& database, const std::string& listing_file) {
    ListingBuild result;
    FileInfo info;
    if (files->info(database, &info) != FileStatus::Ok || info.is_folder) return result;
    Bytes data;
    if (files->read(database, kSongLengthsLimit, &data) != FileStatus::Ok) return result;

    std::vector<Parsed> parsed;
    std::unordered_map<std::string, bool> seen;
    std::size_t lines = 0, skipped = 0;
    enum class Before { Nothing, Path, RefusedPath } before = Before::Nothing;
    std::string path;
    std::string line;
    std::size_t at = 0;
    while (at < data.size()) {
        std::size_t end = at;
        while (end < data.size() && data[end] != '\n') ++end;
        std::size_t cut = end;
        if (cut > at && data[cut - 1] == '\r') --cut;
        if (cut - at > kLineCut) cut = at + kLineCut;
        line.assign(reinterpret_cast<const char*>(data.data()) + at, cut - at);
        const std::size_t zero = line.find('\0');
        if (zero != std::string::npos) line.resize(zero);
        at = end + 1;
        if (line.empty()) continue;
        ++lines;

        if (lines == 1 && line == "[Database]") continue;
        if (line.size() >= 2 && line[0] == ';' && line[1] == ' ') {
            path = line.substr(2);
            if (is_hvsc_path(path)) {
                before = Before::Path;
            } else {
                before = Before::RefusedPath;
                ++skipped;
            }
            continue;
        }
        Parsed tune;
        if (parse_md5_line(line, &tune)) {
            if (before == Before::Path) {
                if (seen.emplace(path, true).second) {
                    tune.path = path;
                    parsed.push_back(std::move(tune));
                }
            } else if (before == Before::Nothing) {
                ++skipped;
            }
            before = Before::Nothing;
            continue;
        }
        ++skipped;
        before = Before::Nothing;
    }
    result.lines_skipped = static_cast<int>(skipped);
    if (parsed.empty() || skipped * 100 > lines) return result;

    std::stable_sort(parsed.begin(), parsed.end(), [](const Parsed& a, const Parsed& b) { return path_before(a.path, b.path); });
    const Bytes file = make_file(parsed, info);
    result.tunes = static_cast<int>(parsed.size());
    result.folders = static_cast<int>(get_u32(file.data() + 32)) - 1;
    result.lengths = static_cast<int>(get_u32(file.data() + 36));
    result.ok = safe_write(files, listing_file, file) == FileStatus::Ok;   // SPEC-132
    return result;
}

Listing::Listing(const Listing& other) : map_(other.snapshot()) {}

Listing& Listing::operator=(const Listing& other) {
    if (this != &other) std::atomic_store(&map_, other.snapshot());
    return *this;
}

std::shared_ptr<const Listing::Map> Listing::snapshot() const { return std::atomic_load(&map_); }

namespace {

// The map of a listing file; null if it cannot be used.
std::shared_ptr<const Listing::Map> read_map(Files* files, const std::string& listing_file, const std::string& database) {
    using Map = Listing::Map;
    auto map = std::make_shared<Map>();
    if (files->map(listing_file, kListingFileLimit, &map->file) != FileStatus::Ok) return nullptr;
    const std::size_t size = map->file.size();
    if (size < kHeaderSize || std::memcmp(map->file.data(), kMagic, sizeof kMagic) != 0) return nullptr;
    const std::uint8_t* h = map->file.data();
    if (!database.empty()) {
        FileInfo info;
        if (files->info(database, &info) != FileStatus::Ok) return nullptr;
        if (get_u64(h + 8) != info.size || static_cast<std::int64_t>(get_u64(h + 16)) != info.modified_s ||
            static_cast<std::int32_t>(get_u32(h + 24)) != info.modified_ns) {
            return nullptr;
        }
    }
    map->tunes = get_u32(h + 28);
    map->folders = get_u32(h + 32);
    map->lengths = get_u32(h + 36);
    map->children = get_u32(h + 40);
    map->text_size = get_u32(h + 44);
    const std::uint64_t T = map->tunes, F = map->folders;
    const std::uint64_t total = kHeaderSize + T * kTuneRecord + F * kFolderRecord + 4 * static_cast<std::uint64_t>(map->children) +
                                4 * static_cast<std::uint64_t>(map->lengths) + 4 * T + 4 * F + 4 * T + map->text_size;
    if (F == 0 || total != size || get_u64(h + 48) != size) return nullptr;
    map->tunes_at = kHeaderSize;
    map->folders_at = map->tunes_at + static_cast<std::size_t>(T) * kTuneRecord;
    map->children_at = map->folders_at + static_cast<std::size_t>(F) * kFolderRecord;
    map->lengths_at = map->children_at + 4 * static_cast<std::size_t>(map->children);
    map->by_path_at = map->lengths_at + 4 * static_cast<std::size_t>(map->lengths);
    map->folders_by_path_at = map->by_path_at + 4 * static_cast<std::size_t>(T);
    map->by_md5_at = map->folders_by_path_at + 4 * static_cast<std::size_t>(F);
    map->text_at = map->by_md5_at + 4 * static_cast<std::size_t>(T);
    return map;
}

}  // namespace

bool Listing::open(Files* files, const std::string& listing_file, const std::string& database) {
    // The new map is made aside and put in place in one step: a reader on another thread holds the map it
    // took for as long as it reads, so it never sees one half made or one taken away under it (SPEC-022).
    const std::shared_ptr<const Map> map = read_map(files, listing_file, database);
    std::atomic_store(&map_, map);
    return map != nullptr;
}

bool Listing::is_open() const { return snapshot() != nullptr; }

int Listing::tune_count() const {
    const std::shared_ptr<const Map> map = snapshot();
    return map ? static_cast<int>(map->tunes) : 0;
}

int Listing::folder_count() const {
    const std::shared_ptr<const Map> map = snapshot();
    return map ? static_cast<int>(map->folders) - 1 : 0;
}

bool Listing::by_path(const std::string& path, ListedTune* out) const {
    const std::shared_ptr<const Map> map = snapshot();
    if (!map) return false;
    const Map& m = *map;
    const std::uint32_t id = m.find(m.by_path_at, m.tunes, path, [&m](std::uint32_t t, std::string* p) { return m.tune_path(t, p); });
    return id != UINT32_MAX && m.tune(id, out);
}

bool Listing::by_md5(const std::string& md5, ListedTune* out) const {
    const std::shared_ptr<const Map> map = snapshot();
    if (!map || md5.size() != 32) return false;
    std::uint8_t wanted[16];
    for (int i = 0; i < 16; ++i) {
        const int high = hex_value(md5[2 * i]), low = hex_value(md5[2 * i + 1]);
        if (high < 0 || low < 0) return false;
        wanted[i] = static_cast<std::uint8_t>(high * 16 + low);
    }
    const Map& m = *map;
    auto md5_of = [&m](std::uint32_t t) -> const std::uint8_t* {
        return t < m.tunes ? m.at(m.tunes_at + t * kTuneRecord + 16) : nullptr;
    };
    std::uint32_t low = 0, high = m.tunes;
    while (low < high) {
        const std::uint32_t middle = low + (high - low) / 2;
        const std::uint8_t* probe = md5_of(get_u32(m.at(m.by_md5_at + 4 * static_cast<std::size_t>(middle))));
        if (probe == nullptr) return false;
        if (std::memcmp(probe, wanted, 16) < 0) low = middle + 1;
        else high = middle;
    }
    if (low >= m.tunes) return false;
    const std::uint32_t id = get_u32(m.at(m.by_md5_at + 4 * static_cast<std::size_t>(low)));
    const std::uint8_t* found = md5_of(id);
    return found != nullptr && std::memcmp(found, wanted, 16) == 0 && m.tune(id, out);
}

bool Listing::folder(const std::string& path, std::vector<std::string>* folders, std::vector<std::string>* tunes) const {
    const std::shared_ptr<const Map> map = snapshot();
    if (!map) return false;
    const Map& m = *map;
    std::uint32_t id = 0;
    if (path != "/") {
        if (path.empty() || path[0] != '/' || path.back() == '/') return false;
        id = m.find(m.folders_by_path_at, m.folders, path, [&m](std::uint32_t f, std::string* p) { return m.folder_path(f, p); });
        if (id == UINT32_MAX) return false;
    }
    const std::uint8_t* record = m.at(m.folders_at + static_cast<std::size_t>(id) * kFolderRecord);
    const std::uint32_t first_child = get_u32(record + 8), child_count = get_u32(record + 12);
    const std::uint32_t first_tune = get_u32(record + 16), tune_count = get_u32(record + 20);
    if (static_cast<std::uint64_t>(first_child) + child_count > m.children ||
        static_cast<std::uint64_t>(first_tune) + tune_count > m.tunes) {
        return false;
    }
    std::vector<std::string> folder_names, tune_names;
    std::string name;
    for (std::uint32_t i = 0; i < child_count; ++i) {
        const std::uint32_t child = get_u32(m.at(m.children_at + 4 * (static_cast<std::size_t>(first_child) + i)));
        if (!m.folder_path(child, &name)) return false;
        folder_names.push_back(name.substr(name.rfind('/') + 1));
    }
    for (std::uint32_t i = 0; i < tune_count; ++i) {
        if (!m.tune_name(first_tune + i, &name)) return false;
        tune_names.push_back(name);
    }
    *folders = std::move(folder_names);
    *tunes = std::move(tune_names);
    return true;
}

std::vector<ListedTune> Listing::tunes(int first, int count) const {
    std::vector<ListedTune> out;
    const std::shared_ptr<const Map> map = snapshot();
    if (!map || count <= 0) return out;
    const std::int64_t from = std::max(first, 0);
    const std::int64_t to = std::min<std::int64_t>(static_cast<std::int64_t>(first) + count, map->tunes);
    for (std::int64_t t = from; t < to; ++t) {
        ListedTune tune;
        if (!map->tune(static_cast<std::uint32_t>(t), &tune)) break;
        out.push_back(std::move(tune));
    }
    return out;
}

}  // namespace spz
