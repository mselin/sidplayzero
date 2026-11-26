// SPDX-License-Identifier: GPL-3.0-or-later
// The listing (SPEC-142): what HVSC's song length database says, in a form that is quick to ask. It is
// built once from DOCUMENTS/Songlengths.md5 into listing.bin in the cache, and then mapped, not loaded.
// From it come the lengths of every subtune, and the tunes and folders HVSC has, on the device or not.
#pragma once

#include <cstddef>
#include <memory>
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
    int folders = 0;               // every folder that holds a tune, at any depth; the root is not counted
    int lengths = 0;
    int lines_skipped = 0;         // lines that fit neither form, or whose path is not an HVSC path
};

// The most bytes of song length database that are read (SPEC-204), and of listing file that is mapped.
const std::size_t kSongLengthsLimit = 32u * 1024 * 1024;
const std::size_t kListingFileLimit = 64u * 1024 * 1024;

// SPEC-142's database is read thus:
//   - Lines end at LF, with a CR before it dropped. A zero byte ends its line, and a line is cut at 4,096
//     bytes (SPEC-204). Empty lines are passed over and not counted.
//   - The first line may be "[Database]". A line "; PATH" names a tune; PATH must be an HVSC path, else the
//     line is skipped and counted, and so is nothing else on its account: the MD5 line after it is passed
//     over without being counted again. A line "MD5=LENGTH LENGTH ..." gives the lengths of the path before
//     it, each m:ss or m:ss.SSS (ss at most 59); an MD5 line with no path before it is skipped and counted.
//     Any other line is skipped and counted. A path given twice keeps its first lengths.
//   - Refused when lines skipped * 100 > lines read, or when no tune is found. A refused database writes
//     nothing and leaves an existing listing file as it was (SPEC-140 checks a download this way before it
//     replaces anything).
//
// The listing file (listing.bin) is our own and is read through Files::map. It records the size and the
// modification time, to the nanosecond, of the database it was built from. Every read of it is checked
// against its bounds, so a damaged file gives "not found", never a read outside it.
//
// A Listing may be opened again on one thread while others read it (SPEC-022): each call reads one whole
// listing, the one before or the one after. A reader that asks several things in one job and wants them all
// from the same listing takes a copy first (`Listing snapshot = *listing;`): a copy shares the mapped file of
// the moment, and keeps it while it lives, however often the original is opened again.
class Listing {
public:
    Listing() = default;
    Listing(const Listing& other);
    Listing& operator=(const Listing& other);

    // Reads the song length database at `database` and writes the listing to `listing_file`, through
    // SPEC-132's safe write (a temporary file beside it, synced, renamed over it, and the folder synced).
    // More than one line in a hundred skipped, or no tune at all, refuses the file.
    static ListingBuild build(Files* files, const std::string& database, const std::string& listing_file);

    // Opens a listing file, mapping it. False if it is not there, or was built from another database than
    // the one now at `database` (by size and modification time), or is damaged. A `database` of "" opens
    // the file without comparing it with any database: for a listing whose database has been moved since
    // (SPEC-144's step 7), or whose database is known to be the one it was built from.
    // A Listing that fails to open is closed, whatever it held before.
    bool open(Files* files, const std::string& listing_file, const std::string& database);
    bool is_open() const;

    int tune_count() const;
    int folder_count() const;      // as ListingBuild::folders

    // The tune at an HVSC path, or the tune with an MD5 (the first, in listing order). False if none.
    bool by_path(const std::string& path, ListedTune* out) const;
    bool by_md5(const std::string& md5, ListedTune* out) const;

    // What HVSC has directly in a folder ("/" is the root): the names of its folders and of its tunes,
    // each in the order of SPEC-042. False if HVSC has no such folder.
    bool folder(const std::string& path, std::vector<std::string>* folders, std::vector<std::string>* tunes) const;

    // Every tune, in listing order, from `first`, at most `count`: for the index (SPEC-161) and the tests.
    std::vector<ListedTune> tunes(int first, int count) const;

    struct Map;   // the mapped file; copies of a Listing share it, and it lives while any of them does

private:
    std::shared_ptr<const Map> snapshot() const;
    std::shared_ptr<const Map> map_;   // read and replaced only through std::atomic_load and std::atomic_store
};

}  // namespace spz
