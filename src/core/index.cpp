// SPDX-License-Identifier: GPL-3.0-or-later
// The search index (SPEC-160, SPEC-161).
//
// index.bin, all numbers little-endian:
//   8 bytes  "SPZINDEX"
//   u32      format number (Index::kFormat)
//   u32      flags: bit 0, the last build ran to its end
//   u64      size of the song length database; i64 its modification time in seconds
//   u16+n    the collection root
//   u32      number of records, then each record:
//     u8 present, u8 chip model, u8 clock, u8 0; u16 year; u64 size; i64 modification time (s)
//     u16+n path, u16+n title, u16+n author, u16+n released, u8+n MD5 (0 or 32 hexadecimal digits)
// and nothing after the last record. The records are in order of path (SPEC-042). The search text is not
// kept in the file: it is made again from the four fields when the file is read.
#include "spz/index.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include "spz/collection.h"
#include "spz/md5.h"
#include "spz/paths.h"
#include "spz/safe_file.h"
#include "spz/tune.h"

namespace spz {

namespace {

const char kMagic[8] = {'S', 'P', 'Z', 'I', 'N', 'D', 'E', 'X'};
const std::size_t kIndexFileLimit = 256u * 1024 * 1024;

// --- search text ----------------------------------------------------------------------------------------

// What each of U+00C0 to U+024F becomes in search text: its base letters in lower case; "" for a
// character that is no letter (a space); nullptr for a letter that is kept as it is.
const char* const kLatinFold[0x250 - 0xC0] = {
    "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e",
    "i", "i", "i", "i", "d", "n", "o", "o", "o", "o", "o", "",
    "o", "u", "u", "u", "u", "y", "th", "ss", "a", "a", "a", "a",
    "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
    "d", "n", "o", "o", "o", "o", "o", "", "o", "u", "u", "u",
    "u", "y", "th", "y", "a", "a", "a", "a", "a", "a", "c", "c",
    "c", "c", "c", "c", "c", "c", "d", "d", "d", "d", "e", "e",
    "e", "e", "e", "e", "e", "e", "e", "e", "g", "g", "g", "g",
    "g", "g", "g", "g", "h", "h", "h", "h", "i", "i", "i", "i",
    "i", "i", "i", "i", "i", "i", "ij", "ij", "j", "j", "k", "k",
    "k", "l", "l", "l", "l", "l", "l", "l", "l", "l", "l", "n",
    "n", "n", "n", "n", "n", "n", "n", "n", "o", "o", "o", "o",
    "o", "o", "oe", "oe", "r", "r", "r", "r", "r", "r", "s", "s",
    "s", "s", "s", "s", "s", "s", "t", "t", "t", "t", "t", "t",
    "u", "u", "u", "u", "u", "u", "u", "u", "u", "u", "u", "u",
    "w", "w", "y", "y", "y", "z", "z", "z", "z", "z", "z", "s",
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "f", nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "o", "o", nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "u",
    "u", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "dz", "dz", "dz", "lj",
    "lj", "lj", "nj", "nj", "nj", "a", "a", "i", "i", "o", "o", "u",
    "u", "u", "u", "u", "u", "u", "u", "u", "u", nullptr, "a", "a",
    "a", "a", nullptr, nullptr, nullptr, nullptr, "g", "g", "k", "k", "o", "o",
    "o", "o", nullptr, nullptr, "j", "dz", "dz", "dz", "g", "g", nullptr, nullptr,
    "n", "n", "a", "a", nullptr, nullptr, nullptr, nullptr, "a", "a", "a", "a",
    "e", "e", "e", "e", "i", "i", "i", "i", "o", "o", "o", "o",
    "r", "r", "r", "r", "u", "u", "u", "u", "s", "s", "t", "t",
    nullptr, nullptr, "h", "h", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "a", "a",
    "e", "e", "o", "o", "o", "o", "o", "o", "o", "o", "y", "y",
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr,
};

// Decodes the UTF-8 character at text[i]: its code point and length. A byte that starts no valid
// character gives length 1 and code point 0xFFFD.
std::uint32_t decode(const std::string& text, std::size_t i, std::size_t* length) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    *length = 1;
    if (c < 0x80) return c;
    std::size_t more;
    std::uint32_t code, least;
    if (c >= 0xC2 && c <= 0xDF) {
        more = 1;
        code = c & 0x1Fu;
        least = 0x80;
    } else if (c >= 0xE0 && c <= 0xEF) {
        more = 2;
        code = c & 0x0Fu;
        least = 0x800;
    } else if (c >= 0xF0 && c <= 0xF4) {
        more = 3;
        code = c & 0x07u;
        least = 0x10000;
    } else {
        return 0xFFFD;
    }
    if (text.size() - i <= more) return 0xFFFD;
    for (std::size_t k = 1; k <= more; ++k) {
        const unsigned char next = static_cast<unsigned char>(text[i + k]);
        if ((next & 0xC0) != 0x80) return 0xFFFD;
        code = (code << 6) | (next & 0x3Fu);
    }
    if (code < least || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) return 0xFFFD;
    *length = more + 1;
    return code;
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

// Adds `text` to `out` as search text: ASCII letters in lower case, digits as they are, accented Latin
// letters as their base letters, other letters as they are (Greek and Cyrillic in lower case), combining
// accents dropped, and everything else a space. Spaces are not yet collapsed.
void fold_into(const std::string& text, std::string* out) {
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t length;
        std::uint32_t c = decode(text, i, &length);
        i += length;
        if (c < 0x80) {
            if (c >= 'A' && c <= 'Z') out->push_back(static_cast<char>(c - 'A' + 'a'));
            else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out->push_back(static_cast<char>(c));
            else out->push_back(' ');
            continue;
        }
        if (c >= 0xC0 && c < 0x250) {
            const char* folded = kLatinFold[c - 0xC0];
            if (folded == nullptr) put_utf8(c, out);
            else if (*folded == '\0') out->push_back(' ');
            else out->append(folded);
            continue;
        }
        if ((c >= 0x300 && c <= 0x36F) || (c >= 0xFE00 && c <= 0xFE0F) || c == 0x200B || c == 0x200D) continue;
        if (c >= 0xFF10 && c <= 0xFF19) { out->push_back(static_cast<char>('0' + (c - 0xFF10))); continue; }
        if (c >= 0xFF21 && c <= 0xFF3A) { out->push_back(static_cast<char>('a' + (c - 0xFF21))); continue; }
        if (c >= 0xFF41 && c <= 0xFF5A) { out->push_back(static_cast<char>('a' + (c - 0xFF41))); continue; }
        const bool no_letter = c < 0xC0 || (c >= 0x2B0 && c <= 0x2FF) || (c >= 0x2000 && c <= 0x2BFF) ||
                               (c >= 0x3000 && c <= 0x303F) || (c >= 0xE000 && c <= 0xF8FF) ||
                               (c >= 0xFE10 && c <= 0xFE6F) || (c >= 0xFF00 && c <= 0xFF65) || c >= 0xFFF0 ||
                               (c >= 0x1F000 && c <= 0x1FAFF) || c == 0x37E || c == 0x387;
        if (no_letter) { out->push_back(' '); continue; }
        if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) c += 0x20;          // Greek capitals
        else if (c >= 0x410 && c <= 0x42F) c += 0x20;                   // Cyrillic capitals
        else if (c >= 0x400 && c <= 0x40F) c += 0x50;
        put_utf8(c, out);
    }
}

// Collapses runs of spaces to one, and drops spaces at either end.
std::string tidy_spaces(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    bool space = false;
    for (const char c : text) {
        if (c == ' ') {
            space = !out.empty();
        } else {
            if (space) out.push_back(' ');
            space = false;
            out.push_back(c);
        }
    }
    return out;
}

// --- the file -------------------------------------------------------------------------------------------

// Reads the file's bytes from the front, refusing any read past the end.
struct Reader {
    const Bytes& bytes;
    std::size_t at = 0;
    bool ok = true;

    bool has(std::size_t n) {
        if (!ok || bytes.size() - at < n) ok = false;
        return ok;
    }
    std::uint64_t number(int width) {
        if (!has(static_cast<std::size_t>(width))) return 0;
        std::uint64_t v = 0;
        for (int i = width - 1; i >= 0; --i) v = (v << 8) | bytes[at + static_cast<std::size_t>(i)];
        at += static_cast<std::size_t>(width);
        return v;
    }
    std::string text(int width) {
        const std::size_t n = static_cast<std::size_t>(number(width));
        if (!has(n)) return std::string();
        std::string out(reinterpret_cast<const char*>(bytes.data() + at), n);
        at += n;
        return out;
    }
};

bool is_md5_text(const std::string& md5) {
    if (md5.empty()) return true;
    if (md5.size() != 32) return false;
    return std::all_of(md5.begin(), md5.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

// Writes into a buffer already of the right size.
struct Writer {
    std::uint8_t* at;
    void number(std::uint64_t v, int width) {
        for (int i = 0; i < width; ++i) *at++ = static_cast<std::uint8_t>(v >> (8 * i));
    }
    void text(const std::string& s, int width, std::size_t most) {
        const std::size_t n = std::min(s.size(), most);
        number(n, width);
        if (n > 0) std::memcpy(at, s.data(), n);
        at += n;
    }
};

std::size_t cut16(const std::string& s) { return std::min<std::size_t>(s.size(), 0xFFFF); }

Bytes write_index(const IndexHeader& header, bool complete, const std::vector<IndexRecord>& records) {
    std::size_t size = sizeof kMagic + 4 + 4 + 8 + 8 + 2 + cut16(header.root) + 4;
    for (const IndexRecord& r : records) {
        size += 4 + 2 + 8 + 8 + 2 + cut16(r.path) + 2 + cut16(r.title) + 2 + cut16(r.author) + 2 + cut16(r.released) + 1 +
                std::min<std::size_t>(r.md5.size(), 0xFF);
    }
    Bytes out(size);
    Writer w{out.data()};
    std::memcpy(w.at, kMagic, sizeof kMagic);
    w.at += sizeof kMagic;
    w.number(static_cast<std::uint32_t>(header.format), 4);
    w.number(complete ? 1u : 0u, 4);
    w.number(header.database_size, 8);
    w.number(static_cast<std::uint64_t>(header.database_modified_s), 8);
    w.text(header.root, 2, 0xFFFF);
    w.number(records.size(), 4);
    for (const IndexRecord& r : records) {
        w.number(r.present ? 1 : 0, 1);
        w.number(static_cast<unsigned>(r.model), 1);
        w.number(static_cast<unsigned>(r.clock), 1);
        w.number(0, 1);
        w.number(static_cast<unsigned>(std::max(0, std::min(r.year, 9999))), 2);
        w.number(r.size, 8);
        w.number(static_cast<std::uint64_t>(r.modified_s), 8);
        w.text(r.path, 2, 0xFFFF);
        w.text(r.title, 2, 0xFFFF);
        w.text(r.author, 2, 0xFFFF);
        w.text(r.released, 2, 0xFFFF);
        w.text(r.md5, 1, 0xFF);
    }
    return out;
}

// The whole read, with the flags too.
bool read_index_file(const Bytes& file, IndexHeader* header, std::vector<IndexRecord>* records, bool* complete) {
    Reader in{file};
    if (!in.has(sizeof kMagic) || std::memcmp(file.data(), kMagic, sizeof kMagic) != 0) return false;
    in.at = sizeof kMagic;
    IndexHeader h;
    h.format = static_cast<int>(in.number(4));
    const std::uint64_t flags = in.number(4);
    h.database_size = in.number(8);
    h.database_modified_s = static_cast<std::int64_t>(in.number(8));
    h.root = in.text(2);
    const std::uint64_t count = in.number(4);
    if (!in.ok || h.format != Index::kFormat || (flags & ~1u) != 0) return false;
    const std::size_t smallest = 4 + 2 + 8 + 8 + 2 * 4 + 1;
    if (count > (file.size() - in.at) / smallest) return false;
    std::vector<IndexRecord> out;
    out.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t n = 0; n < count; ++n) {
        IndexRecord r;
        const std::uint64_t present = in.number(1), model = in.number(1), clock = in.number(1), zero = in.number(1);
        r.year = static_cast<int>(in.number(2));
        r.size = in.number(8);
        r.modified_s = static_cast<std::int64_t>(in.number(8));
        r.path = in.text(2);
        r.title = in.text(2);
        r.author = in.text(2);
        r.released = in.text(2);
        r.md5 = in.text(1);
        if (!in.ok || present > 1 || model > 2 || clock > 2 || zero != 0 || r.year > 9999) return false;
        if (!is_collection_path(r.path) || !is_md5_text(r.md5)) return false;
        if (!out.empty() && !path_comes_before(out.back().path, r.path)) return false;
        r.present = present == 1;
        r.model = static_cast<SidModel>(model);
        r.clock = static_cast<C64Clock>(clock);
        r.search_text = search_text(r.path, r.title, r.author, r.released);
        out.push_back(std::move(r));
    }
    if (in.at != file.size()) return false;
    *header = std::move(h);
    *records = std::move(out);
    if (complete != nullptr) *complete = (flags & 1u) != 0;
    return true;
}

// SPEC-132's safe write: a temporary file beside the target, synced, renamed over it, the folder synced.
bool write_safely(Files* files, const std::string& target, const Bytes& bytes) {
    const std::size_t slash = target.rfind('/');
    const std::string folder = slash == std::string::npos ? "." : slash == 0 ? "/" : target.substr(0, slash);
    const std::string name = slash == std::string::npos ? target : target.substr(slash + 1);
    const std::string temporary = (slash == std::string::npos ? "" : target.substr(0, slash + 1)) + "." + name + kTempEnding;
    if (files->make_folders(folder) != FileStatus::Ok) return false;
    if (files->write(temporary, bytes) != FileStatus::Ok || files->sync(temporary) != FileStatus::Ok ||
        files->rename(temporary, target) != FileStatus::Ok) {
        files->remove(temporary);
        return false;
    }
    files->sync(folder);
    return true;
}

bool is_tune_name(const std::string& name) {
    if (name.size() < 4) return false;
    auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; };
    const std::size_t at = name.size() - 4;
    return name[at] == '.' && lower(name[at + 1]) == 's' && lower(name[at + 2]) == 'i' && lower(name[at + 3]) == 'd';
}

std::string parent_of(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    if (slash == std::string::npos || slash == 0) return "/";
    return path.substr(0, slash);
}

IndexRecord absent_record(const std::string& path) {
    IndexRecord r;
    r.path = path;
    r.search_text = search_text(path, "", "", "");
    return r;
}

}  // namespace

std::string search_text(const std::string& path, const std::string& title, const std::string& author,
                        const std::string& released) {
    std::string raw;
    raw.reserve(path.size() + title.size() + author.size() + released.size() + 4);
    fold_into(path, &raw);
    raw.push_back(' ');
    fold_into(title, &raw);
    raw.push_back(' ');
    fold_into(author, &raw);
    raw.push_back(' ');
    fold_into(released, &raw);
    return tidy_spaces(raw);
}

bool read_index(const Bytes& file, IndexHeader* header, std::vector<IndexRecord>* records) {
    return read_index_file(file, header, records, nullptr);
}

Index::Index(Files* files, Clock* clock, const Listing* listing, std::string root, std::string index_file)
    : files_(files), clock_(clock), listing_(listing), root_(std::move(root)), index_file_(std::move(index_file)) {}

std::string Index::place(const std::string& path) const {
    std::string base = root_;
    while (base.size() > 1 && base.back() == '/') base.pop_back();
    if (path == "/") return base;
    return base + path;
}

bool Index::listed(const std::string& path) const {
    if (listing_ == nullptr || !listing_->is_open()) return false;
    ListedTune tune;
    return listing_->by_path(path, &tune);
}

IndexHeader Index::present_header() const {
    IndexHeader h;
    h.format = kFormat;
    h.root = root_;
    if (listing_ != nullptr && listing_->is_open()) {
        FileInfo info;
        if (files_->info(place("/DOCUMENTS/Songlengths.md5"), &info) == FileStatus::Ok) {
            h.database_size = info.size;
            h.database_modified_s = info.modified_s;
        }
    }
    return h;
}

bool Index::load() {
    loaded_ = true;
    Bytes bytes;
    IndexHeader header;
    std::vector<IndexRecord> records;
    bool complete = false;
    const bool ok = files_->read(index_file_, kIndexFileLimit, &bytes) == FileStatus::Ok &&
                    read_index_file(bytes, &header, &records, &complete);
    std::lock_guard<std::mutex> lock(mutex_);
    have_file_ = ok;
    if (ok) {
        header_ = std::move(header);
        records_ = std::move(records);
        complete_ = complete;
    } else {
        header_ = IndexHeader();
        records_.clear();
        complete_ = false;
    }
    percent_ = complete_ ? 100 : 0;
    return ok;
}

bool Index::save() {
    Bytes bytes;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        bytes = write_index(header_, complete_, records_);
    }
    return write_safely(files_, index_file_, bytes);
}

std::size_t Index::locate(const std::string& path) const {
    return static_cast<std::size_t>(
        std::lower_bound(records_.begin(), records_.end(), path,
                         [](const IndexRecord& r, const std::string& p) { return path_comes_before(r.path, p); }) -
        records_.begin());
}

void Index::put(IndexRecord record, std::size_t* cursor) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t at = records_.size();
    if (cursor != nullptr) {   // records put in order of path are usually just after the last one
        for (std::size_t i = *cursor; i < records_.size() && i < *cursor + 64; ++i) {
            if (records_[i].path == record.path) {
                at = i;
                break;
            }
        }
    }
    if (at == records_.size()) at = locate(record.path);
    if (cursor != nullptr) *cursor = at + 1;
    if (at < records_.size() && records_[at].path == record.path) {
        records_[at] = std::move(record);
    } else {
        records_.insert(records_.begin() + static_cast<std::ptrdiff_t>(at), std::move(record));
    }
}

// Folder by folder, so that what it finds is in order of path without sorting the whole: a folder's
// folders, each with all it holds, in the order of their names, and then its own tunes.
bool Index::walk(const std::string& folder, bool deep, std::vector<OnDevice>* out) const {
    std::vector<FileInfo> found;
    if (files_->list(place(folder), &found) != FileStatus::Ok) return false;
    std::vector<std::string> folders;
    std::vector<OnDevice> tunes;
    for (FileInfo& info : found) {
        if (info.name.empty() || info.name[0] == '.' || info.is_special) continue;
        std::string path = folder == "/" ? "/" + info.name : folder + "/" + info.name;
        if (info.is_folder && !info.is_link) {
            if (deep && !(folder == "/" && info.name == "DOCUMENTS")) folders.push_back(std::move(path));
        } else if (is_tune_name(info.name) && is_collection_path(path)) {
            tunes.push_back(OnDevice{std::move(path), std::move(info)});
        }
    }
    std::sort(folders.begin(), folders.end(), [](const std::string& a, const std::string& b) { return path_comes_before(a, b); });
    std::sort(tunes.begin(), tunes.end(), [](const OnDevice& a, const OnDevice& b) { return name_comes_before(a.info.name, b.info.name); });
    for (const std::string& path : folders) walk(path, true, out);
    out->insert(out->end(), std::make_move_iterator(tunes.begin()), std::make_move_iterator(tunes.end()));
    return true;
}

std::vector<Index::OnDevice> Index::reconcile(const std::vector<OnDevice>& device,
                                              const std::function<bool(const std::string&)>& in_scope) {
    std::vector<OnDevice> to_read;
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<IndexRecord> merged;
    merged.reserve(records_.size() + device.size());
    std::size_t i = 0, j = 0;
    const std::size_t R = records_.size(), D = device.size();
    while (i < R || j < D) {
        const bool device_first = j < D && (i == R || path_comes_before(device[j].path, records_[i].path));
        const bool record_first = !device_first && i < R && (j == D || path_comes_before(records_[i].path, device[j].path));
        if (device_first) {
            // On the device and not in the index: a record that only has its path until its file is read.
            merged.push_back(absent_record(device[j].path));
            to_read.push_back(device[j]);
            ++j;
        } else if (record_first) {
            IndexRecord& record = records_[i];
            if (!in_scope(record.path)) {
                merged.push_back(std::move(record));
            } else if (listed(record.path)) {
                // No longer on the device, and HVSC lists it: absent again (FIND-021).
                if (record.present || !record.title.empty() || !record.md5.empty()) merged.push_back(absent_record(record.path));
                else merged.push_back(std::move(record));
            }   // else: no listing has it, and its record goes
            ++i;
        } else {
            IndexRecord& record = records_[i];
            const OnDevice& tune = device[j];
            if (!record.present || record.size != tune.info.size || record.modified_s != tune.info.modified_s) {
                to_read.push_back(tune);
            }
            merged.push_back(std::move(record));
            ++i;
            ++j;
        }
    }
    records_ = std::move(merged);
    return to_read;
}

IndexRecord Index::read_record(const OnDevice& tune) const {
    IndexRecord r;
    r.path = tune.path;
    Bytes bytes;
    const FileStatus status = files_->read(place(tune.path), kTuneSizeLimit, &bytes);
    if (status == FileStatus::NotFound) return absent_record(tune.path);
    r.present = true;
    r.size = tune.info.size;
    r.modified_s = tune.info.modified_s;
    if (status == FileStatus::Ok) {
        TuneInfo info;
        if (read_tune_info(bytes, &info)) {
            r.title = info.title;
            r.author = info.author;
            r.released = info.released;
            r.model = info.model;
            r.clock = info.clock;
            r.year = info.year;
        }
        if (!listed(tune.path)) r.md5 = md5_hex(bytes);
    }
    r.search_text = search_text(r.path, r.title, r.author, r.released);
    return r;
}

IndexRun Index::build(IndexReason reason, const std::function<bool()>& keep_going) {
    IndexRun run;
    if (!loaded_) load();
    const IndexHeader now = present_header();
    bool fits;
    bool was_complete;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        fits = have_file_ && header_.format == kFormat && header_.root == now.root &&
               header_.database_size == now.database_size && header_.database_modified_s == now.database_modified_s;
        was_complete = complete_;
    }
    bool dirty = false;
    const Millis began = clock_ ? clock_->now_ms() : 0;
    if (!fits || reason == IndexReason::User) {
        // Step one: a record for every tune of the listing, without opening any.
        std::vector<IndexRecord> fresh;
        if (listing_ != nullptr && listing_->is_open()) {
            const std::vector<ListedTune> tunes = listing_->tunes(0, listing_->tune_count());
            fresh.reserve(tunes.size());
            for (const ListedTune& tune : tunes) fresh.push_back(absent_record(tune.path));
            if (!std::is_sorted(fresh.begin(), fresh.end(),
                                [](const IndexRecord& a, const IndexRecord& b) { return path_comes_before(a.path, b.path); })) {
                std::sort(fresh.begin(), fresh.end(),
                          [](const IndexRecord& a, const IndexRecord& b) { return path_comes_before(a.path, b.path); });
            }
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            header_ = now;
            records_ = std::move(fresh);
            complete_ = false;
            have_file_ = true;
        }
        percent_ = 0;
        was_complete = false;
        run.rebuilt = true;
        save();
        run.step_one_ms = clock_ ? clock_->now_ms() - began : 0;
    }
    const Millis step_two_began = clock_ ? clock_->now_ms() : 0;

    // Step two: the folders of the root that are on the device.
    std::vector<OnDevice> device;
    if (walk("/", true, &device)) {
        const std::size_t before = count();
        const std::vector<OnDevice> to_read = reconcile(device, [](const std::string&) { return true; });
        if (static_cast<std::size_t>(count()) != before || !to_read.empty()) dirty = true;
        const std::size_t total = to_read.size();
        int since_save = 0;
        std::size_t cursor = 0;
        for (std::size_t start = 0; start < total; start += kBatchFiles) {
            const std::size_t end = std::min(total, start + static_cast<std::size_t>(kBatchFiles));
            for (std::size_t k = start; k < end; ++k) put(read_record(to_read[k]), &cursor);
            run.tunes_read += static_cast<int>(end - start);
            since_save += static_cast<int>(end - start);
            percent_ = std::min(99, static_cast<int>(end * 100 / total));
            if (since_save >= kSaveEvery) {
                save();
                since_save = 0;
            }
            if (end < total) {
                if (keep_going && !keep_going()) {
                    if (since_save > 0) save();
                    return run;
                }
                if (playing_) clock_->sleep_ms(kPauseMs);
            }
        }
    } else {
        // The root is not there (a card that is not in, say): nothing is changed on its account.
        dirty = dirty || !was_complete;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        complete_ = true;
    }
    percent_ = 100;
    if (dirty || !was_complete) save();
    run.step_two_ms = clock_ ? clock_->now_ms() - step_two_began : 0;
    run.complete = true;
    if (reason == IndexReason::User || reason == IndexReason::AfterSetup) run.notice = "MSG_INDEX_READY";
    return run;
}

void Index::set_playing(bool playing) { playing_ = playing; }

int Index::percent() const { return percent_; }

bool Index::complete() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return complete_;
}

void Index::refresh_folder(const std::string& folder) {
    std::vector<OnDevice> device;
    if (!walk(folder, false, &device)) return;   // a folder that is gone is seen by the next start's step two
    const std::vector<OnDevice> to_read =
        reconcile(device, [&folder](const std::string& path) { return parent_of(path) == folder; });
    for (const OnDevice& tune : to_read) put(read_record(tune));
}

void Index::tune_arrived(const std::string& path) {
    if (!is_collection_path(path)) return;
    OnDevice tune;
    tune.path = path;
    if (files_->info(place(path), &tune.info) != FileStatus::Ok || tune.info.is_folder) return;
    put(read_record(tune));
}

IndexHeader Index::header() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return header_;
}

int Index::count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(records_.size());
}

std::vector<IndexRecord> Index::records(int first, int count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<IndexRecord> out;
    if (count <= 0) return out;
    const std::size_t from = static_cast<std::size_t>(std::max(first, 0));
    const std::size_t to = std::min(records_.size(), from + static_cast<std::size_t>(count));
    for (std::size_t i = from; i < to; ++i) out.push_back(records_[i]);
    return out;
}

bool Index::find(const std::string& path, IndexRecord* out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t at = locate(path);
    if (at >= records_.size() || records_[at].path != path) return false;
    *out = records_[at];
    return true;
}

}  // namespace spz
