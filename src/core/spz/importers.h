// SPDX-License-Identifier: GPL-3.0-or-later
// Importing other players' playlists (SPEC-175 and SPEC-178, part `playlists`; SPEC-176 and SPEC-177,
// part `import-more`). A file is only ever read. Reading it and matching its paths to tunes are apart:
// the readers take bytes and touch no file, and each is one of the readers that SPEC-205 fuzzes.
#pragma once

#include <string>
#include <vector>

#include "spz/files.h"
#include "spz/listing.h"
#include "spz/playlists.h"
#include "spz/types.h"

namespace spz {

// One entry of a file being imported, before it is matched to a tune.
struct ImportLine {
    // The tune as the file names it, once what is not part of the name is taken off: a leading "{N}", an
    // ending "?N" or "/tune_NNN.sid", "file://" with its "%XX" sequences decoded, a leading "<HVSC>"; and
    // with the "BaseDir=" of a Sidplay/w file put in front of a name that is not a full path, as a
    // folder and a name. Backslashes are still here: matching deals with them (SPEC-178).
    std::string text;
    int subtune = 0;        // 0: the file gives none
    std::string original;   // the line as the file has it, for an entry that is not found
};

struct ImportRead {
    std::string name;                 // the file's "#PLAYLIST:" line, "" if it has none
    std::vector<ImportLine> lines;    // in the file's order; the same entry twice is here twice
    // "" when the file was read; "MSG_IMPORT_NOTHING" when nothing in it could be read as a playlist;
    // "MSG_FILE_TOO_LARGE" over kPlaylistFileLimit; "MSG_LIST_CUT" past kPlaylistEntryLimit (SPEC-204).
    std::string message;
};

// SPEC-175: an M3U or M3U8 file. UTF-8 if it is valid UTF-8 and Latin-1 if not; either kind of line
// ending. Each line that is not empty and does not start with "#" is an entry. Its subtune is the first
// of these that applies: a "#SIDTUNE:" line before it; a "#PHOSPHOR:" line before it holding "song=N"; a
// "{N}" at the start of the line; an ending "?N"; an ending "/tune_NNN.sid" after a name ending in ".sid".
ImportRead read_m3u(const Bytes& file);
// SPEC-176: JSidplay2's ".js2", one tune to a line, a leading "<HVSC>" removed. No subtunes.
ImportRead read_js2(const Bytes& file);
// SPEC-176: JSidplay2's ".json", an array of objects, each object's "filename" a tune; anything else in
// the file is ignored. A file that is not an array of objects gives MSG_IMPORT_NOTHING. No subtunes.
ImportRead read_jsidplay2_json(const Bytes& file);
// SPEC-177: Sidplay/w's ".spl", in its format 1 ("[1]" sections with "Filename=" and "Subsong=") and its
// format 2 (blocks beginning "file=", with "subtune="). The names of keys are read without regard to
// case, and keys the app does not know are ignored.
ImportRead read_spl(const Bytes& file);
// The reader that goes with a file's ending: ".m3u", ".m3u8", ".js2", ".json" or ".spl". Any other
// ending gives MSG_IMPORT_NOTHING.
ImportRead read_import(const std::string& file_name, const Bytes& file);

// What the `text` panel after an import says (SPEC-178).
struct ImportReport {
    std::string message;                  // as ImportRead's; with MSG_IMPORT_NOTHING and
                                          // MSG_FILE_TOO_LARGE no playlist was made
    std::string playlist;                 // the new playlist's name, "" if none was made
    int entries = 0;                      // how many entries it holds, those not found among them
    std::vector<std::string> not_found;   // the entries no tune was found for, as the file has them
};

class Importer {
public:
    // `root` is the collection root and `import_folder` the `import` folder of SPEC-134. `listing` may be
    // null, or not open, when the collection is not HVSC's.
    Importer(Files* files, const std::string& root, const std::string& import_folder, const Listing* listing,
             Playlists* playlists);

    // What the `import` panel lists (SPEC-175): the full paths of the files ending in ".m3u", ".m3u8",
    // ".js2", ".json" or ".spl" in the import folder and in the collection root.
    std::vector<std::string> files() const;

    // SPEC-178: the tune a path of an imported file means. Backslashes become slashes; then the first of
    // these that finds a tune in the listing or under the root: the part after "/C64Music/"; the path as
    // it stands; the path with a "/" in front; the path taken relative to `folder_of_file`; the same
    // four without regard to case; the file's name alone, if exactly one tune in the listing has it.
    // Gives the tune's collection path. False if no form finds a tune; a form that is not a collection
    // path (SPEC-154) finds none.
    bool match(const std::string& text, const std::string& folder_of_file, std::string* path) const;

    // Imports one file into a new playlist (SPEC-175). Its name is the file's "#PLAYLIST:" line, or else
    // the file's name without its ending; control characters are removed; while the name is taken, or is
    // "Favourites", " 2", " 3" and so on is added, the name being cut so that with what is added it is
    // at most 40 characters. An entry no tune is found for becomes a missing entry holding the file's
    // own text.
    ImportReport import_file(const std::string& path);

private:
    Files* files_;
    std::string root_;
    std::string import_folder_;
    const Listing* listing_;
    Playlists* playlists_;
};

}  // namespace spz
