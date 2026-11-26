// SPDX-License-Identifier: GPL-3.0-or-later
// SPEC-131, SPEC-135, SPEC-202: what the app does at start with what it kept.
#include "spz/keep_start.h"

#include "spz/path_text.h"
#include "spz/safe_file.h"

namespace spz {

namespace {

// Whether a remembered folder is a plain path under the root: "/", or "/A/B" with no empty, "." or ".."
// part and no control character. Anything else is not looked for at all: the root opens.
bool is_plain_folder(const std::string& folder) {
    if (folder == "/") return true;
    if (folder.empty() || folder[0] != '/' || folder.back() == '/') return false;
    for (const char c : folder) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte == 0x7F || c == '\\') return false;
    }
    std::size_t at = 1;
    while (at <= folder.size()) {
        std::size_t end = folder.find('/', at);
        if (end == std::string::npos) end = folder.size();
        const std::string part = folder.substr(at, end - at);
        if (part.empty() || part == "." || part == "..") return false;
        at = end + 1;
    }
    return true;
}

bool folder_is_there(const std::string& folder, Files* files, const std::string& root, const Listing* listing) {
    FileInfo info;
    if (files->info(root + folder, &info) == FileStatus::Ok && info.is_folder) return true;
    if (listing != nullptr && listing->is_open()) {
        std::vector<std::string> folders, tunes;
        if (listing->folder(folder, &folders, &tunes)) return true;
    }
    return false;
}

// Where a remembered entry's file is: under the root for a collection path, or the path of a "file://" one.
std::string file_of(const std::string& path, const std::string& root) {
    const std::string scheme = "file://";
    if (path.compare(0, scheme.size(), scheme) == 0) return path.substr(scheme.size());
    return root + path;
}

}  // namespace

BrowserPlace browser_place_at_start(const State& state, Files* files, const std::string& root, const Listing* listing) {
    BrowserPlace place;
    place.folder = "/";
    if (!is_plain_folder(state.browser_folder)) return place;
    std::string folder = state.browser_folder;
    while (folder != "/" && !folder_is_there(folder, files, root, listing)) folder = path_text::collection_parent(folder);
    place.folder = folder;
    if (folder == state.browser_folder) place.entry = state.browser_entry;
    return place;
}

bool restore_player(const State& state, QueueSources* sources, PlayerAtStart* player) {
    if (state.current_path.empty()) return false;
    const Entry current{state.current_path, state.current_subtune, ""};
    std::vector<Entry> queue;
    bool made = false;
    if (state.queue_kind == "folder") made = sources->folder_tunes(state.queue_source, &queue);
    else if (state.queue_kind == "playlist") made = sources->playlist_entries(state.queue_source, &queue);
    int place = 0;
    if (made && !queue.empty()) {
        const auto size = static_cast<int>(queue.size());
        const int remembered = state.queue_place;
        if (remembered >= 1 && remembered <= size && queue[static_cast<std::size_t>(remembered - 1)].path == current.path) {
            place = remembered;
        } else {
            // The queue's source has changed since: the current entry's place in it now, if it has one.
            for (int i = 0; i < size && place == 0; ++i) {
                if (queue[static_cast<std::size_t>(i)] == current) place = i + 1;
            }
            for (int i = 0; i < size && place == 0; ++i) {
                if (queue[static_cast<std::size_t>(i)].path == current.path) place = i + 1;
            }
        }
    }
    if (place == 0) {
        // A search, a source that is gone, or one that no longer holds the entry: the entry alone.
        queue = {current};
        place = 1;
    }
    player->restore(current, queue, place);
    return true;
}

bool resume_at_start(const Settings& settings, const State& state, Files* files, const std::string& root,
                     PlayerAtStart* player) {
    if (!settings.resume_on_start || state.current_path.empty()) return false;
    FileInfo info;
    if (files->info(file_of(state.current_path, root), &info) != FileStatus::Ok) return false;
    if (info.is_folder || info.is_special) return false;
    player->play_current();
    return true;
}

CleanedUp clean_up_at_start(Files* files, const Clock* clock, const Folders& folders) {
    CleanedUp cleaned;
    for (const std::string* folder : {&folders.config, &folders.playlists, &folders.imports, &folders.exports,
                                      &folders.state, &folders.cache}) {
        if (!folder->empty()) cleaned.temp_files += remove_temp_files(files, *folder);
    }
    if (!folders.cache.empty()) cleaned.partials = remove_old_partials(files, clock, folders.cache);
    return cleaned;
}

}  // namespace spz
