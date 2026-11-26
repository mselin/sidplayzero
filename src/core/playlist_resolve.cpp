// SPDX-License-Identifier: GPL-3.0-or-later
// Finding an entry's tune (SPEC-173).
#include "spz/playlist_resolve.h"

#include "spz/md5.h"
#include "spz/paths.h"
#include "spz/tune.h"

namespace spz {

namespace {

const char kFileScheme[] = "file://";

bool is_outside(const std::string& path) { return path.rfind(kFileScheme, 0) == 0; }

// Whether there is a file (not a folder, a link's target unseen, or a device) at a full path.
bool is_file(Files* files, const std::string& full) {
    FileInfo info;
    if (files->info(full, &info) != FileStatus::Ok) return false;
    return !info.is_folder && !info.is_special;
}

}  // namespace

EntryResolver::EntryResolver(Files* files, const std::string& root, const Listing* listing, const OwnTunes* own)
    : files_(files), root_(root), listing_(listing), own_(own) {}

EntryFacts EntryResolver::resolve(const Entry& entry) const {
    EntryFacts facts;
    facts.found = Found::Missing;
    facts.path = entry.path;

    if (is_outside(entry.path)) {
        // "file://" and a full path, as it is: present while its file is there; nothing else can find it.
        const std::string full = entry.path.substr(sizeof kFileScheme - 1);
        if (!full.empty() && full[0] == '/' && full.find('\0') == std::string::npos && is_file(files_, full)) {
            facts.found = Found::Present;
        }
        return facts;
    }
    if (!is_collection_path(entry.path)) return facts;

    const bool listed = listing_ != nullptr && listing_->is_open();

    // 1. A file at its path.
    if (is_file(files_, root_ + entry.path)) {
        facts.found = Found::Present;
        return facts;
    }
    // 2. A tune with its MD5 at another path: in the listing, or among the user's own.
    if (!entry.md5.empty()) {
        ListedTune tune;
        if (listed && listing_->by_md5(entry.md5, &tune)) {
            facts.path = tune.path;
            facts.found = is_file(files_, root_ + tune.path) ? Found::Present : Found::Absent;
            return facts;
        }
        std::string own_path;
        if (own_ != nullptr && own_->own_tune_by_md5(entry.md5, &own_path) && is_collection_path(own_path) &&
            is_file(files_, root_ + own_path)) {
            facts.path = own_path;
            facts.found = Found::Present;
            return facts;
        }
    }
    // 3. The listing has its path: absent. 4. Else missing.
    ListedTune tune;
    if (listed && listing_->by_path(entry.path, &tune)) facts.found = Found::Absent;
    return facts;
}

EntryFacts EntryResolver::facts(const Entry& entry) const {
    EntryFacts facts = resolve(entry);
    if (facts.found == Found::Missing) return facts;

    const bool outside = is_outside(facts.path);
    const bool listed = listing_ != nullptr && listing_->is_open();
    ListedTune tune;
    const bool in_listing = !outside && listed && listing_->by_path(facts.path, &tune);

    int subtune = entry.subtune;
    if (facts.found == Found::Present) {
        const std::string full = outside ? facts.path.substr(sizeof kFileScheme - 1) : root_ + facts.path;
        Bytes file;
        if (files_->read(full, kTuneSizeLimit, &file) == FileStatus::Ok) {
            facts.md5 = md5_hex(file);
            TuneInfo info;
            if (read_tune_info(file, &info)) {
                facts.author = info.author;
                facts.title = info.title;
                if (subtune <= 0) subtune = info.default_song;
            }
        }
    } else {
        facts.md5 = tune.md5;
        // No file to say which subtune is the default: only a tune of one subtune has a known length.
        if (subtune <= 0 && tune.lengths.size() == 1) subtune = 1;
    }
    if (in_listing && subtune >= 1 && static_cast<std::size_t>(subtune) <= tune.lengths.size()) {
        facts.length_ms = tune.lengths[static_cast<std::size_t>(subtune) - 1];
    }
    return facts;
}

}  // namespace spz
