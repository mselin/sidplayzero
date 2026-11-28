// SPDX-License-Identifier: GPL-3.0-or-later
// The collection (SPEC-040 to SPEC-042, SPEC-047, SPEC-048): where it is on the device, and what one
// folder of it shows. A folder's entries come from two sources, the folder as it is on the device and,
// when the root has a listing, what HVSC lists there (SPEC-041). Nothing here draws, and nothing here
// uses the network.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "spz/files.h"
#include "spz/listing.h"
#include "spz/types.h"

namespace spz {

// SPEC-040: where the collection is.
struct CollectionRoot {
    std::string path;              // the root, with a leading "~" replaced by the home folder. It need not exist
    bool from_hvsc_base = false;   // true: it comes from HVSC_BASE; Settings shows it with "(from HVSC_BASE)"
                                   // and the row cannot be changed
};

// SPEC-040: the collection root is the first of these that is set: the environment variable HVSC_BASE,
// the setting `collection`, "~/C64Music". `hvsc_base` is the variable's value, "" when it is not set;
// `setting` is the setting's value, "" when the user has not set it; `home` is the home folder.
CollectionRoot find_collection_root(const std::string& hvsc_base, const std::string& setting,
                                    const std::string& home);

// SPEC-042: whether the name `a` comes before the name `b` in a folder: compared without regard to the
// case of ASCII letters, with ties broken by plain byte order. Folders come before tunes; that is the
// caller's to see to.
bool name_comes_before(const std::string& a, const std::string& b);

// SPEC-042, SPEC-162 (FIND-018): whether the tune at path `a` comes before the tune at path `b` in the order
// of path: folder by folder, a folder's folders before its tunes, each by name_comes_before. Both are paths
// under the root such as "/MUSICIANS/H/Hubbard_Rob/Commando.sid". The listing, the index and search
// results are in this order.
bool path_comes_before(const std::string& a, const std::string& b);

// SPEC-041: a name as the screen shows it: the name itself if it is valid UTF-8, else with "?" in place
// of each byte that is not.
std::string shown_name(const std::string& name);

// One entry of a folder (SPEC-041).
struct FolderEntry {
    std::string name;         // as it is on the device, byte for byte, or as HVSC lists it
    std::string shown;        // shown_name(name)
    bool is_folder = false;
    bool present = false;     // a tune whose file is on the device; false for an absent one. Not used for a folder
    std::string path;         // a tune's collection path, such as "/MUSICIANS/H/Hubbard_Rob/Commando.sid";
                              // "" when its path is not a collection path (SPEC-154), as for a name that is
                              // not valid UTF-8: such a tune still plays, from file_path(), but cannot be
                              // made a favourite, added to a playlist or remembered between runs
    // SPEC-046: of an absent tune, from the listing as the folder is listed, so that its detail line needs
    // no worker: `listed` says the two are known.
    bool listed = false;
    int subtunes = 0;
    Millis first_length_ms = 0;
};

// What one folder shows (SPEC-041), in the order of SPEC-042: folders, then tunes.
struct FolderListing {
    std::string folder;                 // "/" for the root, else such as "/MUSICIANS/H"
    std::vector<FolderEntry> entries;
    std::string notice;                 // "" or "MSG_FOLDER_UNREADABLE", and then there are no entries
    bool on_device = false;             // the folder itself is on the device
    std::int64_t modified_s = 0;        // its modification time when it was listed (SPEC-043); 0 if not on the device
    std::int32_t modified_ns = 0;       // and the nanoseconds past that second (FIND-019)
};

// SPEC-046 (part `detail`): what the detail line says of the highlighted entry.
struct EntryDetail {
    bool is_folder = false;         // for a folder the line says nothing
    bool present = false;
    std::string title, author;      // of a present tune, from its file's header, as UTF-8
    int subtunes = 0;               // of a present tune from its header; of an absent one from the listing
    Millis length_ms = 0;           // of a present tune: its default subtune's length, looked up by the
                                    // file's MD5 (SPEC-063), else `default_length_ms`; of an absent tune:
                                    // the length of its first subtune, from the listing
};

// The collection itself. It keeps nothing of the device in memory: every call looks afresh, through
// `files`. What is kept between two looks is the browser model's business (SPEC-043).
class Collection {
public:
    // `root` is find_collection_root()'s path. `listing` is the root's listing (SPEC-142); nullptr, or a
    // listing that is not open, means the root has none, and the collection is then just the folder tree
    // as it is on the device, with every tune present (SPEC-047).
    Collection(Files* files, std::string root, const Listing* listing);

    const std::string& root() const;
    // SPEC-047: whether the root has a listing, so that tunes can be absent.
    bool has_listing() const;

    // SPEC-041, SPEC-042: the entries of a folder. Only folders, and files whose names end in ".sid" in
    // any case; no name starting with a dot; not the root's DOCUMENTS folder; a tune that the device and
    // HVSC both name appears once. Called on the quick worker (SPEC-043).
    FolderListing list(const std::string& folder) const;

    // SPEC-048: whether the root exists and holds at least one entry. While it does not, the empty
    // screen takes the browser's place.
    bool has_entries() const;

    // Where on the device an entry of a folder is, or would be: the root, the folder and the name joined.
    std::string file_path(const std::string& folder, const std::string& name) const;

    // SPEC-043: a folder's modification time on the device, to the nanosecond, for the browser's look at
    // whether a folder it keeps has changed. False, with both 0, when the folder is not on the device.
    bool folder_time(const std::string& folder, std::int64_t* seconds, std::int32_t* nanoseconds) const;

    // SPEC-046 (part `detail`): reads what the detail line shows for one entry of a folder. For a present
    // tune this reads its file: the header, and the MD5 for the length. Called on the quick worker, once
    // the highlight has rested on the entry for 150 ms.
    EntryDetail detail(const std::string& folder, const FolderEntry& entry, Millis default_length_ms) const;

private:
    // The place on the device of a folder of the collection ("/" is the root).
    std::string folder_place(const std::string& folder) const;
    bool listing_open() const;

    Files* files_;
    std::string root_;
    const Listing* listing_;
};

}  // namespace spz
