// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/lists_panels.h"

#include <memory>
#include <vector>

#include "spz/importers.h"
#include "spz/keymap.h"
#include "spz/listing.h"
#include "spz/messages.h"
#include "spz/path_text.h"
#include "spz/playlist.h"
#include "spz/playlists.h"
#include "ui/panel.h"

namespace spz::ui {

namespace {

const char* const kNewPlaylist = "New playlist\xE2\x80\xA6";

std::string name_without_ending(const std::string& path) {
    std::string name = path_text::last_part(path);
    const std::size_t dot = name.rfind('.');
    if (dot != std::string::npos && dot > 0) name = name.substr(0, dot);
    return name;
}

bool blank(const std::string& text) { return text.find_first_not_of(" \t") == std::string::npos; }

std::string lower_ending(const std::string& name) {
    const std::size_t dot = name.rfind('.');
    return dot == std::string::npos ? "" : text::ascii_lower(name.substr(dot));
}

// SPEC-115: the endings of the part `import-more` (SPEC-176, SPEC-177) only when it is built.
bool importable_in_this_build(const std::string& path) {
    const std::string ending = lower_ending(path_text::last_part(path));
    if (ending == ".js2" || ending == ".json" || ending == ".spl") return part_built("import-more");
    return ending == ".m3u" || ending == ".m3u8";
}

// What the quick worker makes of a file to import: its entries matched to tunes (SPEC-178), and the name it
// asks for (SPEC-175). The playlist itself is made on the UI thread, where the playlists are kept.
struct ImportWork {
    std::string message;
    std::string wanted;
    std::vector<PlaylistEntry> entries;
    std::vector<std::string> not_found;
};

ImportWork read_and_match(Files* files, const std::string& root, const std::string& import_folder,
                          const Listing* listing, const std::string& path) {
    ImportWork work;
    Bytes bytes;
    const FileStatus status = files->read(path, kPlaylistFileLimit, &bytes);
    if (status == FileStatus::TooLarge) {
        work.message = "MSG_FILE_TOO_LARGE";
        return work;
    }
    if (status != FileStatus::Ok) {
        work.message = "MSG_IMPORT_NOTHING";
        return work;
    }
    const std::size_t slash = path.rfind('/');
    const std::string folder = slash == std::string::npos ? std::string(".") : path.substr(0, slash);
    const ImportRead read = read_import(path_text::last_part(path), bytes);
    work.message = read.message;
    if (read.message == "MSG_FILE_TOO_LARGE" || read.message == "MSG_IMPORT_NOTHING") return work;
    const Importer importer(files, root, import_folder, listing, nullptr);
    for (const ImportLine& line : read.lines) {
        PlaylistEntry entry;
        std::string found;
        if (importer.match(line.text, folder, &found)) {
            entry.entry.path = found;
            entry.entry.subtune = line.subtune;
        } else {
            entry.entry.path = line.original;     // a missing entry holding the file's own text
            work.not_found.push_back(line.original);
        }
        work.entries.push_back(entry);
    }
    // The name: the file's "#PLAYLIST:" line, or its name without the ending; no control characters.
    for (const char c : read.name.empty() ? name_without_ending(path) : read.name) {
        const unsigned char byte = static_cast<unsigned char>(c);
        if (byte >= 0x20 && byte != 0x7F) work.wanted += c;
    }
    if (!lists_text::is_valid_utf8(work.wanted)) work.wanted = lists_text::latin1_to_utf8(work.wanted);
    if (blank(work.wanted)) work.wanted = name_without_ending(path);
    if (blank(work.wanted)) work.wanted = "Imported";
    return work;
}

// SPEC-175, SPEC-178: the playlist made from what the worker read, and the `text` panel that tells it.
void finish_import(Shell* shell, const ImportWork& work) {
    if (work.message == "MSG_FILE_TOO_LARGE") {
        shell->notice(work.message);
        return;
    }
    if (work.message == "MSG_IMPORT_NOTHING") {
        shell->open_panel(std::make_unique<TextPanel>(shell, "text", "Import", message_words(work.message)));
        return;
    }
    Playlists* playlists = shell->services().playlists;
    if (playlists == nullptr) return;
    std::string made;
    std::string failure;
    for (int n = 1; n < 100000 && made.empty(); ++n) {
        const std::string added = n == 1 ? std::string() : " " + std::to_string(n);
        const std::string name = lists_text::utf8_prefix(work.wanted, kPlaylistNameLimit - added.size()) + added;
        const ListsResult result = playlists->create(name, work.entries);
        if (result.ok) made = name;
        else if (result.message != "MSG_NAME_TAKEN") {
            failure = result.message;
            break;
        }
    }
    if (made.empty()) {
        if (!failure.empty()) shell->notice(failure);
        return;
    }
    if (work.message == "MSG_LIST_CUT") shell->notice(work.message);
    const std::size_t count = work.entries.size();
    std::string text = std::to_string(count) + (count == 1 ? " entry" : " entries") + " imported into \"" + made + "\".";
    if (!work.not_found.empty()) {
        text += "\nNot found (" + std::to_string(work.not_found.size()) + "):";
        for (const std::string& line : work.not_found) text += "\n" + line;
    }
    shell->redraw();
    shell->open_panel(std::make_unique<TextPanel>(shell, "text", "Import", text));
}

}  // namespace

void open_new_playlist(Shell* shell, std::function<void(const std::string& name)> made) {
    shell->open_panel(std::make_unique<PromptPanel>(
        shell, "New playlist", "", kPlaylistNameLimit, [shell, made](const std::string& text) {
            Playlists* playlists = shell->services().playlists;
            if (playlists == nullptr) return true;
            const ListsResult result = playlists->create(text);
            if (!result.ok) {
                shell->notice(result.message.empty() ? "MSG_INVALID_VALUE" : result.message, result.about);
                return false;
            }
            shell->redraw();
            if (made) made(text);
            return true;
        }));
}

void open_pick_playlist(Shell* shell, const Entry& entry) {
    Playlists* playlists = shell->services().playlists;
    if (playlists == nullptr) return;
    std::vector<Row> rows;
    std::vector<std::string> names;
    for (const PlaylistSummary& summary : playlists->list()) {
        Row row;
        row.text = summary.name;
        row.kind = "folder";
        rows.push_back(row);
        names.push_back(summary.name);
    }
    Row add;
    add.text = kNewPlaylist;
    add.kind = "action";
    rows.push_back(add);
    shell->open_panel(std::make_unique<ListPanel>(shell, "pick-playlist", "Add to playlist", rows,
                                                  [shell, names, entry](std::size_t chosen) {
        Playlists* lists = shell->services().playlists;
        if (lists == nullptr) return;
        const auto add_to = [shell, entry](const std::string& name) {
            Playlists* now = shell->services().playlists;
            if (now == nullptr) return;
            const ListsResult result = now->add(name, entry);
            if (!result.message.empty()) shell->notice(result.message, result.about);
            shell->redraw();
        };
        if (chosen < names.size()) {
            add_to(names[chosen]);
            return;
        }
        // "New playlist…": its name first, then the entry goes into it.
        open_new_playlist(shell, add_to);
    }));
}

void open_import(Shell* shell) {
    const Services& s = shell->services();
    if (s.playlists == nullptr || s.files == nullptr) return;
    Files* files = s.files;
    const std::string root = s.collection;
    const std::string import_folder = s.import_folder;
    const Listing* listing = s.listing;
    shell->quick()->post([shell, files, root, import_folder, listing] {
        const Importer importer(files, root, import_folder, listing, nullptr);
        std::vector<std::string> found;
        for (const std::string& path : importer.files()) {
            if (importable_in_this_build(path)) found.push_back(path);
        }
        shell->ui()->post([shell, files, root, import_folder, listing, found] {
            std::vector<Row> rows;
            for (const std::string& path : found) {
                Row row;
                row.text = path.compare(0, import_folder.size() + 1, import_folder + "/") == 0
                               ? path_text::last_part(path)
                               : path_text::last_part(root) + "/" + path_text::last_part(path);
                row.kind = "action";
                rows.push_back(row);
            }
            shell->open_panel(std::make_unique<ListPanel>(shell, "import", "Import", rows,
                                                          [shell, files, root, import_folder, listing, found](std::size_t i) {
                if (i >= found.size()) return;
                const std::string path = found[i];
                shell->quick()->post([shell, files, root, import_folder, listing, path] {
                    const ImportWork work = read_and_match(files, root, import_folder, listing, path);
                    shell->ui()->post([shell, work] { finish_import(shell, work); });
                });
            }));
        });
    });
}

}  // namespace spz::ui
