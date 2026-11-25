// SPDX-License-Identifier: GPL-3.0-or-later
// Notes from STIL (SPEC-097, the part `notes`): the entry HVSC's DOCUMENTS/STIL.txt has for a tune.
//
// STIL is Latin-1 text. An entry starts with a line that is the tune's HVSC path, and ends at the next
// empty line. Its first lines, up to a line of the form "(#n)", are about the file; the lines after a
// "(#n)" line are about subtune n.
#pragma once

#include <cstddef>
#include <string>

#include "spz/files.h"

namespace spz {

// What the panel says when there is no STIL file, or no entry for the tune.
extern const char* const kNoNotes;      // "No notes for this tune"

// SPEC-204: STIL.txt is read at most this far.
const std::size_t kStilSizeLimit = 16 * 1024 * 1024;

struct StilNotes {
    bool found = false;     // STIL has an entry for the tune
    std::string text;       // what the panel shows, as UTF-8: the entry's part for the file, then its part
                            // for the subtune; each line as STIL has it, with its leading spaces, the lines
                            // joined by "\n"; without the path's line and the "(#n)" line. kNoNotes if not found
    std::string notice;     // "MSG_FILE_TOO_LARGE" if the file is longer than the limit, else ""
};

// Reads `stil_file` and gives the notes for the tune at an HVSC path and its subtune, counted from 1.
// Called on the quick worker.
StilNotes stil_notes(Files* files, const std::string& stil_file, const std::string& path, int subtune);

}  // namespace spz
