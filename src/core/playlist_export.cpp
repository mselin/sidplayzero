// SPDX-License-Identifier: GPL-3.0-or-later
// "Export" (SPEC-179).
#include "spz/playlist_export.h"

#include "spz/playlist.h"

namespace spz {

namespace {

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

Bytes exported_playlist(const Playlist& playlist) {
    Bytes out;
    put(&out, "#EXTM3U");
    put(&out, "#PLAYLIST:" + clean(playlist.name));
    for (const PlaylistEntry& entry : playlist.entries) {
        if (!entry.title.empty()) {
            std::string text = clean(entry.title);
            if (!entry.author.empty()) text = clean(entry.author) + " - " + text;
            put(&out, "#EXTINF:" + std::to_string(entry.length_s < 0 ? -1 : entry.length_s) + "," + text);
        }
        std::string line = entry.entry.path;
        if (entry.entry.subtune > 0) line += "?" + std::to_string(entry.entry.subtune);
        put(&out, line);
    }
    return out;
}

ListsResult export_playlist(Files* files, const Playlists& playlists, const std::string& name,
                            const std::string& export_folder) {
    ListsResult result;
    const Playlist* playlist = playlists.find(name);
    if (playlist == nullptr) return result;
    const std::string path = export_folder + "/" + playlist_file_name(name);
    const FileStatus status = lists_files::write_safely(files, path, exported_playlist(*playlist));
    if (status != FileStatus::Ok) {
        if (status == FileStatus::NoSpace) result.message = "MSG_CARD_FULL";
        return result;
    }
    result.ok = true;
    result.message = "MSG_EXPORTED";
    result.about["path"] = path;
    return result;
}

}  // namespace spz
