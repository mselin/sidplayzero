// SPDX-License-Identifier: GPL-3.0-or-later
// The playlists (SPEC-170 to SPEC-173): the files of the playlists folder, and every change to them.
#include "spz/playlists.h"

#include <algorithm>

namespace spz {

namespace lists_files {

FileStatus write_safely(Files* files, const std::string& target, const Bytes& bytes) {
    const std::size_t slash = target.rfind('/');
    const std::string folder = slash == std::string::npos ? std::string(".") : target.substr(0, slash == 0 ? 1 : slash);
    const std::string name = slash == std::string::npos ? target : target.substr(slash + 1);
    const std::string temporary = (slash == std::string::npos ? std::string() : target.substr(0, slash + 1)) + "." +
                                  name + ".tmp-sidplayzero";
    FileStatus status = files->make_folders(folder);
    if (status != FileStatus::Ok) return status;
    status = files->write(temporary, bytes);
    if (status == FileStatus::Ok) status = files->sync(temporary);
    if (status == FileStatus::Ok) status = files->rename(temporary, target);
    if (status != FileStatus::Ok) {
        files->remove(temporary);
        return status;
    }
    // The rename is done: the target holds the new bytes whatever the flush of the folder says.
    files->sync(folder);
    return FileStatus::Ok;
}

}  // namespace lists_files

namespace {

using lists_text::ascii_lower;
using lists_text::same_ignoring_case;

bool ends_with(const std::string& s, const std::string& end) {
    return s.size() >= end.size() && s.compare(s.size() - end.size(), end.size(), end) == 0;
}

std::string last_part(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

ListsResult failed(const std::string& message = "") {
    ListsResult result;
    result.ok = false;
    result.message = message;
    return result;
}

// A save that failed: SPEC-116 has a message for a full card, and none for any other failure.
ListsResult not_saved(FileStatus status) { return failed(status == FileStatus::NoSpace ? "MSG_CARD_FULL" : ""); }

ListsResult done(const std::string& message = "") {
    ListsResult result;
    result.ok = true;
    result.message = message;
    return result;
}

}  // namespace

Playlists::Playlists(Files* files, const std::string& folder, const EntryResolver* resolver)
    : files_(files), folder_(folder), resolver_(resolver) {}

int Playlists::index_of(const std::string& name) const {
    for (std::size_t i = 0; i < playlists_.size(); ++i) {
        if (playlists_[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

bool Playlists::is_taken(const std::string& name, int except) const {
    if (same_ignoring_case(name, kFavouritesName) && !(except >= 0 && playlists_[static_cast<std::size_t>(except)].name == kFavouritesName)) {
        return true;
    }
    for (std::size_t i = 0; i < playlists_.size(); ++i) {
        if (static_cast<int>(i) == except) continue;
        if (same_ignoring_case(playlists_[i].name, name)) return true;
    }
    return false;
}

// "" when no name can be had: the folder cannot be looked at (an error of Files::info() other than
// NotFound, such as NoAccess, says nothing of whether a name is free), or every name up to
// kMostFileNumbers is taken. The caller then saves nothing.
std::string Playlists::free_file_name(const std::string& name, int except) {
    const int kMostFileNumbers = 10000;
    const std::string wanted = playlist_file_name(name);
    const std::string stem = wanted.substr(0, wanted.size() - 5);
    for (int n = 1; n <= kMostFileNumbers; ++n) {
        const std::string candidate = n == 1 ? wanted : stem + " " + std::to_string(n) + ".m3u8";
        bool taken = false;
        for (std::size_t i = 0; i < file_names_.size() && !taken; ++i) {
            if (static_cast<int>(i) == except) continue;
            taken = same_ignoring_case(file_names_[i], candidate);
        }
        if (!taken && except >= 0 && same_ignoring_case(file_names_[static_cast<std::size_t>(except)], candidate)) {
            return candidate;   // its own file, under the new name's spelling
        }
        if (!taken) {
            FileInfo info;
            const FileStatus status = files_->info(folder_ + "/" + candidate, &info);
            if (status == FileStatus::NotFound) return candidate;
            if (status != FileStatus::Ok) return "";
        }
    }
    return "";
}

FileStatus Playlists::save(std::size_t index) {
    Playlist& list = playlists_[index];
    // SPEC-173: an entry found by its MD5 at another path is written at that path from now on.
    if (resolver_ != nullptr) {
        for (PlaylistEntry& entry : list.entries) {
            if (entry.entry.md5.empty()) continue;
            const EntryFacts facts = resolver_->resolve(entry.entry);
            if (facts.found != Found::Missing && facts.path != entry.entry.path) entry.entry.path = facts.path;
        }
    }
    return lists_files::write_safely(files_, folder_ + "/" + file_names_[index], write_playlist(list));
}

void Playlists::load() {
    playlists_.clear();
    file_names_.clear();
    std::vector<FileInfo> found;
    files_->list(folder_, &found);
    std::sort(found.begin(), found.end(), [](const FileInfo& a, const FileInfo& b) { return a.name < b.name; });
    for (const FileInfo& info : found) {
        if (info.is_folder || info.is_special || info.name.empty() || info.name[0] == '.') continue;
        if (!ends_with(ascii_lower(info.name), ".m3u8")) continue;
        Bytes bytes;
        if (files_->read(folder_ + "/" + info.name, kPlaylistFileLimit, &bytes) != FileStatus::Ok) continue;
        PlaylistRead read = read_playlist(bytes);
        if (read.message == "MSG_FILE_TOO_LARGE") continue;
        Playlist& list = read.playlist;
        if (list.name.empty() || !is_playlist_name(list.name)) {
            list.name = lists_text::utf8_prefix(info.name.substr(0, info.name.size() - 5), kPlaylistNameLimit);
        }
        if (same_ignoring_case(list.name, kFavouritesName)) {
            if (index_of(kFavouritesName) >= 0) continue;
            list.name = kFavouritesName;
        } else if (is_taken(list.name, -1)) {
            continue;   // two files of one name: the first is the playlist
        }
        if (!is_playlist_name(list.name)) continue;
        playlists_.push_back(std::move(list));
        file_names_.push_back(info.name);
    }
    if (index_of(kFavouritesName) < 0) {
        Playlist favourites;
        favourites.name = kFavouritesName;
        playlists_.push_back(favourites);
        file_names_.push_back(free_file_name(kFavouritesName, -1));
    }
}

std::vector<PlaylistSummary> Playlists::list() const {
    std::vector<PlaylistSummary> rows;
    for (const Playlist& list : playlists_) {
        PlaylistSummary row;
        row.name = list.name;
        row.entries = static_cast<int>(list.entries.size());
        row.favourites = list.name == kFavouritesName;
        rows.push_back(row);
    }
    std::stable_sort(rows.begin(), rows.end(), [](const PlaylistSummary& a, const PlaylistSummary& b) {
        if (a.favourites != b.favourites) return a.favourites;
        return ascii_lower(a.name) < ascii_lower(b.name);
    });
    return rows;
}

const Playlist* Playlists::find(const std::string& name) const {
    const int i = index_of(name);
    return i < 0 ? nullptr : &playlists_[static_cast<std::size_t>(i)];
}

std::string Playlists::file_of(const std::string& name) const {
    const int i = index_of(name);
    return i < 0 ? std::string() : folder_ + "/" + file_names_[static_cast<std::size_t>(i)];
}

ListsResult Playlists::create(const std::string& name) { return create(name, std::vector<PlaylistEntry>()); }

ListsResult Playlists::create(const std::string& name, const std::vector<PlaylistEntry>& entries) {
    if (!is_playlist_name(name)) return failed();
    if (is_taken(name, -1)) return failed("MSG_NAME_TAKEN");
    Playlist list;
    list.name = name;
    list.entries = entries;
    const std::string file = free_file_name(name, -1);
    if (file.empty()) return not_saved(FileStatus::Failed);
    playlists_.push_back(list);
    file_names_.push_back(file);
    if (const FileStatus status = save(playlists_.size() - 1); status != FileStatus::Ok) {
        playlists_.pop_back();
        file_names_.pop_back();
        return not_saved(status);
    }
    ListsResult result = done();
    result.about["name"] = name;
    return result;
}

ListsResult Playlists::rename(const std::string& name, const std::string& new_name) {
    const int i = index_of(name);
    if (i < 0 || name == kFavouritesName) return failed();
    if (!is_playlist_name(new_name)) return failed();
    if (is_taken(new_name, i)) return failed("MSG_NAME_TAKEN");
    const std::size_t index = static_cast<std::size_t>(i);
    const Playlist before = playlists_[index];
    const std::string old_file = file_names_[index];
    const std::string new_file = free_file_name(new_name, i);
    if (new_file.empty()) return not_saved(FileStatus::Failed);

    playlists_[index].name = new_name;
    file_names_[index] = new_file;
    if (const FileStatus status = save(index); status != FileStatus::Ok) {
        playlists_[index] = before;
        file_names_[index] = old_file;
        return not_saved(status);
    }
    if (new_file != old_file) {
        // Names that differ only in case may be one file on some file systems: remove the old one only
        // when the folder holds both.
        bool both = true;
        if (same_ignoring_case(new_file, old_file)) {
            std::vector<FileInfo> found;
            files_->list(folder_, &found);
            bool old_there = false, new_there = false;
            for (const FileInfo& info : found) {
                old_there = old_there || info.name == old_file;
                new_there = new_there || info.name == new_file;
            }
            both = old_there && new_there;
        }
        if (both) {
            files_->remove(folder_ + "/" + old_file);
            files_->sync(folder_);
        }
    }
    ListsResult result = done();
    result.about["name"] = new_name;
    return result;
}

ListsResult Playlists::remove(const std::string& name) {
    const int i = index_of(name);
    if (i < 0 || name == kFavouritesName) return failed();
    const std::size_t index = static_cast<std::size_t>(i);
    const FileStatus status = files_->remove(folder_ + "/" + file_names_[index]);
    if (status != FileStatus::Ok) return not_saved(status);
    files_->sync(folder_);
    playlists_.erase(playlists_.begin() + i);
    file_names_.erase(file_names_.begin() + i);
    return done();
}

ListsResult Playlists::add(const std::string& name, const Entry& entry) {
    const int i = index_of(name);
    if (i < 0) return failed();
    const std::size_t index = static_cast<std::size_t>(i);
    for (const PlaylistEntry& there : playlists_[index].entries) {
        if (there.entry == entry) {
            ListsResult result = failed("MSG_ALREADY_THERE");
            result.about["name"] = name;
            return result;
        }
    }
    if (playlists_[index].entries.size() >= kPlaylistEntryLimit) return failed("MSG_LIST_CUT");

    PlaylistEntry added;
    added.entry = entry;
    if (resolver_ != nullptr) {
        const EntryFacts facts = resolver_->facts(entry);
        if (facts.found != Found::Missing) added.entry.path = facts.path;
        if (!facts.md5.empty()) added.entry.md5 = facts.md5;
        added.author = facts.author;
        added.title = facts.title;
        added.length_s = facts.length_ms < 0 ? -1 : static_cast<int>(facts.length_ms / 1000);
    }
    playlists_[index].entries.push_back(added);
    if (const FileStatus status = save(index); status != FileStatus::Ok) {
        playlists_[index].entries.pop_back();
        return not_saved(status);
    }
    ListsResult result = done("MSG_ADDED");
    result.about["name"] = name;
    return result;
}

ListsResult Playlists::remove_entry(const std::string& name, std::size_t at) {
    const int i = index_of(name);
    if (i < 0) return failed();
    const std::size_t index = static_cast<std::size_t>(i);
    std::vector<PlaylistEntry>& entries = playlists_[index].entries;
    if (at >= entries.size()) return failed();
    const PlaylistEntry taken = entries[at];
    entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(at));
    if (const FileStatus status = save(index); status != FileStatus::Ok) {
        playlists_[index].entries.insert(playlists_[index].entries.begin() + static_cast<std::ptrdiff_t>(at), taken);
        return not_saved(status);
    }
    return done();
}

ListsResult Playlists::move_entry(const std::string& name, std::size_t at, int by) {
    const int i = index_of(name);
    if (i < 0 || (by != -1 && by != 1)) return failed();
    const std::size_t index = static_cast<std::size_t>(i);
    std::vector<PlaylistEntry>& entries = playlists_[index].entries;
    if (at >= entries.size()) return failed();
    if ((by < 0 && at == 0) || (by > 0 && at + 1 == entries.size())) return done();   // at the end: it stays
    const std::size_t other = by < 0 ? at - 1 : at + 1;
    std::swap(entries[at], entries[other]);
    if (const FileStatus status = save(index); status != FileStatus::Ok) {
        std::swap(playlists_[index].entries[at], playlists_[index].entries[other]);
        return not_saved(status);
    }
    return done();
}

std::vector<PlaylistRow> Playlists::rows(const std::string& name) const {
    std::vector<PlaylistRow> out;
    const Playlist* list = find(name);
    if (list == nullptr) return out;
    for (const PlaylistEntry& entry : list->entries) {
        PlaylistRow row;
        row.text = entry.title.empty() ? last_part(entry.entry.path) : entry.title;
        row.subtune = entry.entry.subtune;
        row.found = resolver_ != nullptr ? resolver_->resolve(entry.entry).found : Found::Missing;
        out.push_back(row);
    }
    return out;
}

std::vector<Entry> Playlists::queue(const std::string& name) const {
    std::vector<Entry> out;
    const Playlist* list = find(name);
    if (list == nullptr) return out;
    for (const PlaylistEntry& entry : list->entries) {
        Entry queued = entry.entry;
        if (resolver_ != nullptr) {
            const EntryFacts facts = resolver_->resolve(entry.entry);
            if (facts.found != Found::Missing) queued.path = facts.path;
        }
        out.push_back(queued);
    }
    return out;
}

}  // namespace spz
