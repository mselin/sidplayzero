// SPDX-License-Identifier: GPL-3.0-or-later
// The listing (SPEC-142): what HVSC's song length database says, in a form that is quick to ask. It is
// built once from DOCUMENTS/Songlengths.md5 into listing.bin in the cache, and then mapped, not loaded.
// From it come the lengths of every subtune, and the tunes and folders HVSC has, on the device or not.
#pragma once

#include <string>
#include <vector>

#include "spz/files.h"
#include "spz/types.h"

namespace spz {

struct ListedTune {
    std::string path;              // an HVSC path (SPEC-154)
    std::string md5;               // 32 lower-case hexadecimal digits
    std::vector<Millis> lengths;   // one for each subtune, in milliseconds
};

struct ListingBuild {
    bool ok = false;               // false: the file was refused (MSG_LENGTHS_UNREADABLE) and there is no listing
    int tunes = 0;
    int folders = 0;
    int lengths = 0;
    int lines_skipped = 0;         // lines that fit neither form, or whose path is not an HVSC path
};

class Listing {
public:
    // Reads the song length database at `database` and writes the listing to `listing_file`, through
    // SPEC-132's safe write. More than one line in a hundred skipped, or no tune at all, refuses the file.
    static ListingBuild build(Files* files, const std::string& database, const std::string& listing_file);

    // Opens a listing file. False if it is not there, or was built from another database than the one
    // now at `database` (by size and modification time), or is damaged.
    bool open(Files* files, const std::string& listing_file, const std::string& database);
    bool is_open() const;

    int tune_count() const;
    int folder_count() const;

    // The tune at an HVSC path, or the tune with an MD5 (the first, in listing order). False if none.
    bool by_path(const std::string& path, ListedTune* out) const;
    bool by_md5(const std::string& md5, ListedTune* out) const;

    // What HVSC has directly in a folder ("/" is the root): the names of its folders and of its tunes,
    // each in the order of SPEC-042. False if HVSC has no such folder.
    bool folder(const std::string& path, std::vector<std::string>* folders, std::vector<std::string>* tunes) const;

    // Every tune, in listing order, from `first`, at most `count`: for the index (SPEC-161) and the tests.
    std::vector<ListedTune> tunes(int first, int count) const;
};

}  // namespace spz
