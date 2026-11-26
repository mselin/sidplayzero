// SPDX-License-Identifier: GPL-3.0-or-later
// The collection (SPEC-040 to SPEC-042, SPEC-046 to SPEC-048): where it is, and what one folder of it shows.
#include "spz/collection.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <utility>

#include "spz/md5.h"
#include "spz/paths.h"
#include "spz/tune.h"

namespace spz {

namespace {

unsigned char folded(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    return (u >= 'A' && u <= 'Z') ? static_cast<unsigned char>(u - 'A' + 'a') : u;
}

// <0, 0 or >0 for a[ab, ae) against b[bb, be) in the order of SPEC-042: ASCII letters as lower case
// (FIND-018), the shorter of two that start alike first, and a tie broken by plain bytes.
int compare_names(const std::string& a, std::size_t ab, std::size_t ae, const std::string& b, std::size_t bb,
                  std::size_t be) {
    const std::size_t an = ae - ab, bn = be - bb, n = std::min(an, bn);
    for (std::size_t i = 0; i < n; ++i) {
        const unsigned char x = folded(a[ab + i]), y = folded(b[bb + i]);
        if (x != y) return x < y ? -1 : 1;
    }
    if (an != bn) return an < bn ? -1 : 1;
    for (std::size_t i = 0; i < n; ++i) {
        const unsigned char x = static_cast<unsigned char>(a[ab + i]), y = static_cast<unsigned char>(b[bb + i]);
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

// Whether a file's name makes it a tune of the collection: it ends in ".sid" in any case (SPEC-041).
bool is_tune_name(const std::string& name) {
    if (name.size() < 4) return false;
    const std::size_t at = name.size() - 4;
    return name[at] == '.' && folded(name[at + 1]) == 's' && folded(name[at + 2]) == 'i' && folded(name[at + 3]) == 'd';
}

// "/" and "A" give "/A"; "/A" and "B" give "/A/B".
std::string child_of(const std::string& folder, const std::string& name) {
    return folder == "/" ? "/" + name : folder + "/" + name;
}

// The length in bytes of the valid UTF-8 character at text[i], or 0 if the byte there does not start one.
std::size_t utf8_length(const std::string& text, std::size_t i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c < 0x80) return 1;
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
        return 0;
    }
    if (text.size() - i <= more) return 0;
    for (std::size_t k = 1; k <= more; ++k) {
        const unsigned char next = static_cast<unsigned char>(text[i + k]);
        if ((next & 0xC0) != 0x80) return 0;
        code = (code << 6) | (next & 0x3Fu);
    }
    if (code < least || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) return 0;
    return more + 1;
}

}  // namespace

CollectionRoot find_collection_root(const std::string& hvsc_base, const std::string& setting, const std::string& home) {
    CollectionRoot root;
    std::string chosen;
    if (!hvsc_base.empty()) {   // an empty HVSC_BASE counts as unset (FIND-024)
        chosen = hvsc_base;
        root.from_hvsc_base = true;
    } else if (!setting.empty()) {
        chosen = setting;
    } else {
        chosen = "~/C64Music";
    }
    if (chosen == "~") {
        chosen = home;
    } else if (chosen.size() >= 2 && chosen[0] == '~' && chosen[1] == '/') {
        chosen = home + chosen.substr(1);
    }
    root.path = chosen;
    return root;
}

bool name_comes_before(const std::string& a, const std::string& b) {
    return compare_names(a, 0, a.size(), b, 0, b.size()) < 0;
}

bool path_comes_before(const std::string& a, const std::string& b) {
    std::size_t i = a.empty() || a[0] != '/' ? 0 : 1;
    std::size_t j = b.empty() || b[0] != '/' ? 0 : 1;
    for (;;) {
        std::size_t ea = a.find('/', i), eb = b.find('/', j);
        const bool folder_a = ea != std::string::npos, folder_b = eb != std::string::npos;
        if (!folder_a) ea = a.size();
        if (!folder_b) eb = b.size();
        if (folder_a != folder_b) return folder_a;   // a folder's folders before its tunes
        const int c = compare_names(a, i, ea, b, j, eb);
        if (c != 0) return c < 0;
        if (!folder_a) return false;
        i = ea + 1;
        j = eb + 1;
    }
}

std::string shown_name(const std::string& name) {
    std::string shown;
    shown.reserve(name.size());
    std::size_t i = 0;
    while (i < name.size()) {
        const std::size_t length = utf8_length(name, i);
        if (length == 0) {
            shown += '?';
            ++i;
        } else {
            shown.append(name, i, length);
            i += length;
        }
    }
    return shown;
}

Collection::Collection(Files* files, std::string root, const Listing* listing)
    : files_(files), root_(std::move(root)), listing_(listing) {}

const std::string& Collection::root() const { return root_; }

bool Collection::listing_open() const { return listing_ != nullptr && listing_->is_open(); }

bool Collection::has_listing() const { return listing_open(); }

std::string Collection::folder_place(const std::string& folder) const {
    std::string place = root_;
    while (place.size() > 1 && place.back() == '/') place.pop_back();
    if (folder != "/" && !folder.empty()) place += folder;
    return place;
}

std::string Collection::file_path(const std::string& folder, const std::string& name) const {
    std::string place = folder_place(folder);
    if (place.empty() || place.back() != '/') place += '/';
    return place + name;
}

bool Collection::folder_time(const std::string& folder, std::int64_t* seconds, std::int32_t* nanoseconds) const {
    *seconds = 0;
    *nanoseconds = 0;
    FileInfo info;
    if (files_->info(folder_place(folder), &info) != FileStatus::Ok) return false;
    *seconds = info.modified_s;
    *nanoseconds = info.modified_ns;
    return true;
}

FolderListing Collection::list(const std::string& folder) const {
    FolderListing out;
    out.folder = folder;
    const bool at_root = folder == "/";

    std::vector<FileInfo> found;
    const FileStatus status = files_->list(folder_place(folder), &found);
    if (status != FileStatus::Ok && status != FileStatus::NotFound) {
        out.notice = "MSG_FOLDER_UNREADABLE";
        return out;
    }
    if (status == FileStatus::Ok) {
        out.on_device = true;
        FileInfo self;
        if (files_->info(folder_place(folder), &self) == FileStatus::Ok) {
            out.modified_s = self.modified_s;
            out.modified_ns = self.modified_ns;
        }
    } else {
        found.clear();
    }

    std::vector<FolderEntry> folders, tunes;
    for (const FileInfo& info : found) {
        if (info.name.empty() || info.name[0] == '.') continue;
        if (info.is_special) continue;
        if (info.is_folder && !info.is_link) {
            if (at_root && info.name == "DOCUMENTS") continue;
            FolderEntry entry;
            entry.name = info.name;
            entry.is_folder = true;
            folders.push_back(std::move(entry));
        } else if (is_tune_name(info.name)) {   // a file, or a link that is named as one
            FolderEntry entry;
            entry.name = info.name;
            entry.present = true;
            tunes.push_back(std::move(entry));
        }
    }

    if (listing_open()) {
        std::vector<std::string> listed_folders, listed_tunes;
        if (listing_->folder(folder, &listed_folders, &listed_tunes)) {
            std::unordered_set<std::string> device_folders, device_tunes;
            for (const FolderEntry& e : folders) device_folders.insert(e.name);
            for (const FolderEntry& e : tunes) device_tunes.insert(e.name);
            for (const std::string& name : listed_folders) {
                if (at_root && name == "DOCUMENTS") continue;
                if (device_folders.count(name) != 0) continue;
                FolderEntry entry;
                entry.name = name;
                entry.is_folder = true;
                folders.push_back(std::move(entry));
            }
            for (const std::string& name : listed_tunes) {
                if (device_tunes.count(name) != 0) continue;
                FolderEntry entry;
                entry.name = name;
                entry.present = false;
                tunes.push_back(std::move(entry));
            }
        }
    }

    auto by_name = [](const FolderEntry& a, const FolderEntry& b) { return name_comes_before(a.name, b.name); };
    std::sort(folders.begin(), folders.end(), by_name);
    std::sort(tunes.begin(), tunes.end(), by_name);
    for (FolderEntry& entry : tunes) {
        const std::string path = child_of(folder, entry.name);
        if (is_collection_path(path)) entry.path = path;
    }
    out.entries.reserve(folders.size() + tunes.size());
    for (FolderEntry& entry : folders) {
        entry.shown = shown_name(entry.name);
        out.entries.push_back(std::move(entry));
    }
    for (FolderEntry& entry : tunes) {
        entry.shown = shown_name(entry.name);
        out.entries.push_back(std::move(entry));
    }
    return out;
}

bool Collection::has_entries() const {
    std::vector<FileInfo> found;
    if (files_->list(folder_place("/"), &found) != FileStatus::Ok) return false;
    if (listing_open() && listing_->tune_count() > 0) return true;
    for (const FileInfo& info : found) {
        if (info.name.empty() || info.name[0] == '.' || info.is_special) continue;
        if (info.is_folder && !info.is_link) {
            if (info.name != "DOCUMENTS") return true;
        } else if (is_tune_name(info.name)) {
            return true;
        }
    }
    return false;
}

EntryDetail Collection::detail(const std::string& folder, const FolderEntry& entry, Millis default_length_ms) const {
    EntryDetail detail;
    if (entry.is_folder) {
        detail.is_folder = true;
        return detail;
    }
    detail.present = entry.present;
    if (!entry.present) {
        ListedTune listed;
        if (listing_open() && listing_->by_path(child_of(folder, entry.name), &listed)) {
            detail.subtunes = static_cast<int>(listed.lengths.size());
            if (!listed.lengths.empty()) detail.length_ms = listed.lengths[0];
        }
        return detail;
    }
    detail.length_ms = default_length_ms;
    Bytes bytes;
    if (files_->read(file_path(folder, entry.name), kTuneSizeLimit, &bytes) != FileStatus::Ok) return detail;
    TuneInfo info;
    if (!read_tune_info(bytes, &info)) return detail;
    detail.title = info.title;
    detail.author = info.author;
    detail.subtunes = info.songs;
    if (listing_open()) {
        ListedTune listed;
        if (listing_->by_md5(md5_hex(bytes), &listed)) {
            const int song = info.default_song > 0 ? info.default_song : 1;
            if (song <= static_cast<int>(listed.lengths.size())) {
                detail.length_ms = listed.lengths[static_cast<std::size_t>(song - 1)];
            }
        }
    }
    return detail;
}

}  // namespace spz
