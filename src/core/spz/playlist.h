// SPDX-License-Identifier: GPL-3.0-or-later
// A playlist and its file (SPEC-170, part `playlists`). The file is an extended M3U file in UTF-8 that
// other players can read, with two kinds of line of the app's own:
//
//     #EXTM3U
//     #PLAYLIST:Loading tunes
//     #EXTINF:235,Rob Hubbard - Commando
//     #SIDTUNE:subtune=1 md5=6d019ecba831a9f853675aac29a61c10
//     /MUSICIANS/H/Hubbard_Rob/Commando.sid
//
// What is here reads and writes that text and touches no file: the store that keeps the files is
// spz/playlists.h.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "spz/limits.h"
#include "spz/text.h"
#include "spz/text_field.h"
#include "spz/types.h"

namespace spz {

// SPEC-204's limits of a playlist's file, its entries and its lines are spz/limits.h's: kPlaylistFileLimit,
// kPlaylistEntryLimit, kLineLimit.
// SPEC-172: a playlist's name is 1 to this many characters: kPlaylistNameLimit, in spz/text_field.h (one
// definition for the two headers, so that a file may include both).
// SPEC-171: the playlist that always exists.
extern const char* const kFavouritesName;   // "Favourites"

// One entry of a playlist, with the lines that belong to it in the file (SPEC-170).
struct PlaylistEntry {
    // The tune's line: a collection path (SPEC-154), or "file://" and a full path for a tune outside the
    // collection, as it is in the file. A line that is neither is kept as it is, and the entry is missing
    // (SPEC-154, SPEC-173). The subtune and the MD5 are those of the "#SIDTUNE:" line: 0 and "" without.
    Entry entry;
    // From "#EXTINF:seconds,author - title", when the file has that line; "" and -1 when not known.
    std::string author;
    std::string title;
    int length_s = -1;
    // Every other "#" line that came before this entry, whole and in order. They stay with it when the
    // entries are reordered, and are written before it again.
    std::vector<std::string> other_lines;
};

struct Playlist {
    std::string name;                        // "#PLAYLIST:", as the user typed it: what the app shows
    std::vector<PlaylistEntry> entries;
    std::vector<std::string> last_lines;     // "#" lines after the last entry, kept at the end
};

struct PlaylistRead {
    Playlist playlist;
    // "" when the whole file was read; "MSG_FILE_TOO_LARGE" when it is over kPlaylistFileLimit, and then
    // nothing was read; "MSG_LIST_CUT" when only the first kPlaylistEntryLimit entries were (SPEC-204).
    std::string message;
};

// Reads a playlist's file (SPEC-170). Never fails otherwise: a line it cannot use is kept or skipped. A
// zero byte ends its line, and a line over kLineLimit is cut (SPEC-204). This is one of the
// readers that SPEC-205 fuzzes.
PlaylistRead read_playlist(const Bytes& file);

// The file's text for a playlist (SPEC-170): "#EXTM3U", "#PLAYLIST:", then for each entry its other
// lines, "#EXTINF:" when the title or the length is known, "#SIDTUNE:" when it has a subtune number or
// an MD5, and the tune's line. Valid UTF-8, each line ended by a line feed (SPEC-009).
Bytes write_playlist(const Playlist& playlist);

// The name of a playlist's file (SPEC-170): the playlist's name with every character other than
// letters, digits, space, "-" and "_" replaced by "_", then ".m3u8". "Rock/Pop" gives "Rock_Pop.m3u8".
std::string playlist_file_name(const std::string& name);

// Whether a name may be a playlist's (SPEC-172): 1 to kPlaylistNameLimit characters, none of them a
// control character. Whether the name is taken is the store's to say.
bool is_playlist_name(const std::string& name);

// The helpers of text that the files of the lists use (playlists, importing, exporting): spz/text.h's, and
// the lines of a playlist's file.
namespace lists_text {
using text::ascii_lower;
using text::is_valid_utf8;
using text::latin1_to_utf8;
using text::same_ignoring_case;
using text::utf8_length;
using text::utf8_prefix;
// The lines of a file: each ended by LF, CR LF or CR; a zero byte ends its line and a line is cut at
// kLineLimit bytes, the rest of it dropped (SPEC-204).
std::vector<std::string> split_lines(const Bytes& file);
// A line as UTF-8: as it is when it is valid UTF-8 (a character cut at the limit dropped), else read as
// Latin-1.
std::string as_utf8(std::string line);
}  // namespace lists_text

}  // namespace spz
