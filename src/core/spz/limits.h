// SPDX-License-Identifier: GPL-3.0-or-later
// Reading files that come from outside, with limits (SPEC-204): how large a file of each kind may be, how
// long a line, and how a file's bytes become lines. Every reader of such a file in `core` reads through
// this, on a worker, so that no file, however large or odd, keeps the app from answering keys for long.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "spz/files.h"
#include "spz/types.h"

namespace spz {

// The kinds of file SPEC-204's table has a limit for.
enum class FileKind {
    Settings,         // settings.ini                         64 KiB
    State,            // state.ini                            64 KiB
    HvSids,           // DOCUMENTS/hv_sids.txt                64 KiB
    Playlist,         // a playlist                            4 MiB
    Import,           // a file being imported                 4 MiB
    HvscText,         // DOCUMENTS/HVSC.txt                    1 MiB
    BugList,          // DOCUMENTS/BUGlist.txt                 1 MiB
    Stil,             // DOCUMENTS/STIL.txt                   16 MiB
    SongLengths,      // the song length database             32 MiB
    ArchiveListing,   // what 7z prints of an archive         64 MiB
};

// A line longer than this many bytes is cut there: every reader of lines (the song length database, STIL,
// playlists, imports, the kept files) cuts them here.
const std::size_t kLineLimit = 4096;
// A playlist's file, and a file being imported, is read at most this far: size_limit() of FileKind::Playlist
// and FileKind::Import.
const std::size_t kPlaylistFileLimit = 4 * 1024 * 1024;
// A playlist holds at most this many entries; the rest of its file is not read (MSG_LIST_CUT).
const std::size_t kPlaylistEntryLimit = 20000;

// The most bytes a file of this kind may have.
std::size_t size_limit(FileKind kind);

// Whether a file of this kind may be `size` bytes long: "" if it may, else the identifier of the message
// that refuses it: "MSG_LENGTHS_UNREADABLE" for the song length database, "MSG_FILE_TOO_LARGE" for the rest.
std::string refuse_size(FileKind kind, std::uint64_t size);

// Reads a whole file of a kind, asking Files for no more than the kind's limit. A larger file is not
// read at all: the result is FileStatus::TooLarge, `out` is left empty, and `why` gets what refuse_size()
// says. For any other outcome `why` is "".
FileStatus read_limited(Files* files, const std::string& path, FileKind kind, Bytes* out, std::string* why);

// A file's bytes as lines. Lines end at LF; a CR just before the LF is not part of the line; the last line
// need not end in LF. A zero byte ends its line: the line is what came before it, and the rest, up to the
// line's end, belongs to no line. A line longer than kLineLimit bytes is cut there: the line is its first
// kLineLimit bytes, and the rest, up to the line's end, belongs to no line. Takes time in proportion to
// the bytes, whatever they are.
std::vector<std::string> split_lines(const Bytes& data);

}  // namespace spz
