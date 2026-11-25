// SPDX-License-Identifier: GPL-3.0-or-later
// Finding an entry's tune (SPEC-173, part `playlists`): whether the tune a playlist's entry names is on
// the device, can be fetched, or is nowhere, and where it is now if it has moved.
#pragma once

#include <string>

#include "spz/files.h"
#include "spz/listing.h"
#include "spz/types.h"

namespace spz {

// A tune with this MD5 among the user's own: the tunes under the collection root that HVSC does not
// list. The search index holds the MD5 of each (SPEC-160) and is what answers this in the app; it
// belongs to another part of `core`, so only this much of it is named here.
class OwnTunes {
public:
    virtual ~OwnTunes() = default;
    // The collection path of one of the user's own tunes whose file has this MD5 (32 lower-case
    // hexadecimal digits). False if there is none.
    virtual bool own_tune_by_md5(const std::string& md5, std::string* path) const = 0;
};

// The terms of the specification's section 1.
enum class Found {
    Present,   // the tune's file is on the device
    Absent,    // HVSC lists the tune and its file is not on the device yet: it is fetched when played
    Missing,   // no tune on the device and none that HVSC lists: shown with its mark, skipped by playback
};

// What is known of an entry's tune now.
struct EntryFacts {
    Found found = Found::Missing;
    // Where the tune is: the entry's own path, or, when the tune was found by its MD5, the path it has
    // now. For a missing entry, the entry's text as it is.
    std::string path;
    // The rest is filled in by facts() only; "" and -1 where not known.
    std::string md5;       // of the file when it is present; the listing's for an absent tune
    std::string author;    // from the file's header, as UTF-8
    std::string title;
    Millis length_ms = -1; // the listing's length of the entry's subtune, or of the tune's default subtune
                           // when the entry has no number (SPEC-063)
};

class EntryResolver {
public:
    // `root` is the collection root. `listing` may be null, or not open, when the collection is not
    // HVSC's; `own` may be null when there is no index yet.
    EntryResolver(Files* files, const std::string& root, const Listing* listing, const OwnTunes* own);

    // SPEC-173, in its order: a file at the entry's path; else, if the entry has an MD5, a tune with that
    // MD5 at another path, found in the listing or, for the user's own tunes, through `own`; else, if
    // the listing has the entry's path, the tune is absent; else the entry is missing. An entry whose
    // path is neither a collection path (SPEC-154) nor "file://" and a full path is missing. Reads no
    // tune's contents.
    EntryFacts resolve(const Entry& entry) const;

    // resolve(), and what the playlist's file says of the tune (SPEC-170): the MD5, the author and title,
    // and the length. Reads the tune's file when it is present.
    EntryFacts facts(const Entry& entry) const;

private:
    Files* files_;
    std::string root_;
    const Listing* listing_;
    const OwnTunes* own_;
};

}  // namespace spz
