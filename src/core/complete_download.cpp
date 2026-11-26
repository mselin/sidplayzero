// SPDX-License-Identifier: GPL-3.0-or-later
// The complete download (SPEC-144): when it is offered (SPEC-145), the reader of 7z's listing and the
// check of the archive (SPEC-149), the room it needs (SPEC-146), and the job with its marker file.
//
// The marker, install.state in the cache, is a few lines of text: "step = N", and "archive = NAME", the
// name of the archive's partial file in the cache, so that a run taken up again at the next start can
// delete it.
#include "spz/complete_download.h"

#include <algorithm>
#include <atomic>
#include <utility>

#include "hvsc_files.h"
#include "spz/download_resume.h"
#include "spz/listing.h"
#include "spz/md5.h"
#include "spz/paths.h"
#include "spz/safe_file.h"
#include "spz/tune.h"

namespace spz {

using namespace hvsc_detail;

namespace {

const std::uint64_t kMiB = 1024ull * 1024ull;
const char kMarkerName[] = "install.state";
const char kStagingName[] = ".sidplayzero-staging";
const char kTopFolder[] = "C64Music";
const std::size_t kMarkerLimit = 4096;
const char kFallbackArchiveName[] = "HVSC-archive.7z";

std::string marker_path(const std::string& cache) { return cache + "/" + kMarkerName; }

bool is_digit(char c) { return c >= '0' && c <= '9'; }

// A decimal number of at most 20 digits that fits in 64 bits.
bool parse_size(const std::string& text, std::uint64_t* out) {
    if (text.empty() || text.size() > 20) return false;
    std::uint64_t value = 0;
    for (const char c : text) {
        if (!is_digit(c)) return false;
        const std::uint64_t digit = static_cast<std::uint64_t>(c - '0');
        if (value > (UINT64_MAX - digit) / 10) return false;
        value = value * 10 + digit;
    }
    *out = value;
    return true;
}

// Whether a word of `7z l -slt`'s Attributes, or the value of a Mode line, is a Unix mode such as
// "-rw-r--r--" or "lrwxrwxrwx": a letter for its kind, then nine of the permissions. The letter may be
// one the reader does not know ("?"); kind_of_mode() says what it means.
bool is_unix_mode(const std::string& word) {
    if (word.size() != 10) return false;
    for (std::size_t i = 1; i < word.size(); ++i) {
        if (std::string("-rwxsStT").find(word[i]) == std::string::npos) return false;
    }
    return true;
}

// What a Unix mode's first letter says the entry is. A kind the reader does not know is refused as a
// special file is (SPEC-149, FIND-026).
void kind_of_mode(char kind, ArchiveEntry* entry) {
    switch (kind) {
        case '-': break;
        case 'd': entry->is_folder = true; break;
        case 'l': entry->is_link = true; break;
        default: entry->is_special = true; break;   // b, c, p, s, and any other
    }
}

// Whether a word is eight hexadecimal digits: 7-Zip writes so the Windows attribute bits it has no letter for.
bool is_hex_word(const std::string& word) {
    if (word.size() != 8) return false;
    for (const char c : word) {
        if (!is_digit(c) && !(c >= 'A' && c <= 'F') && !(c >= 'a' && c <= 'f')) return false;
    }
    return true;
}

std::vector<std::string> split_words(const std::string& text) {
    std::vector<std::string> words;
    std::string word;
    for (const char c : text) {
        if (c == ' ' || c == '\t') {
            if (!word.empty()) words.push_back(word);
            word.clear();
        } else {
            word += c;
        }
    }
    if (!word.empty()) words.push_back(word);
    return words;
}

// An entry's Attributes: a word of Windows letters ("D" a folder; "L" a reparse point, which is how
// Windows keeps a link; "d" a device, "8" a volume), perhaps a word of hexadecimal bits, perhaps a Unix
// mode. Any other word is of a kind the reader does not know, and the entry is refused as special.
void read_attributes(const std::string& value, ArchiveEntry* entry) {
    const std::vector<std::string> words = split_words(value);
    for (std::size_t i = 0; i < words.size(); ++i) {
        const std::string& word = words[i];
        if (is_unix_mode(word)) {
            kind_of_mode(word[0], entry);
        } else if (i == 0 && word.find_first_not_of("RHS8DAdNTsLCOIEVXPU_") == std::string::npos) {
            if (word.find('D') != std::string::npos) entry->is_folder = true;
            if (word.find('L') != std::string::npos) entry->is_link = true;
            if (word.find_first_of("d8") != std::string::npos) entry->is_special = true;
        } else if (!is_hex_word(word)) {
            entry->is_special = true;
        }
    }
}

// The name of the archive's file in the cache: the last part of its address, if it is a plain name.
std::string archive_name(const std::string& url) {
    std::string name = url.substr(url.rfind('/') + 1);
    const std::size_t query = name.find_first_of("?#");
    if (query != std::string::npos) name.resize(query);
    bool plain = !name.empty() && name.size() <= 200 && name[0] != '.';
    for (const char c : name) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || is_digit(c) || c == '_' || c == '-' || c == '.';
        plain = plain && ok;
    }
    return plain ? name : kFallbackArchiveName;
}

std::uint64_t add_saturating(std::uint64_t a, std::uint64_t b) { return a > UINT64_MAX - b ? UINT64_MAX : a + b; }

struct Marker {
    int step = 0;
    std::string archive;
};

Marker read_marker(Files* files, const std::string& cache) {
    Marker marker;
    Bytes bytes;
    if (files->read(marker_path(cache), kMarkerLimit, &bytes) != FileStatus::Ok) return marker;
    std::string line;
    std::vector<std::string> lines;
    for (const std::uint8_t c : bytes) {
        if (c == '\n') {
            lines.push_back(line);
            line.clear();
        } else if (c != '\r') {
            line += static_cast<char>(c);
        }
    }
    lines.push_back(line);
    for (const std::string& l : lines) {
        if (l.size() == 8 && l.compare(0, 7, "step = ") == 0 && l[7] >= '1' && l[7] <= '8') marker.step = l[7] - '0';
        if (l.compare(0, 10, "archive = ") == 0) marker.archive = archive_name(l.substr(10));
    }
    if (marker.step == 0) marker.archive.clear();
    return marker;
}

bool write_marker(Files* files, const std::string& cache, int step, const std::string& archive) {
    if (step <= 0) return files->remove(marker_path(cache)) == FileStatus::Ok;
    std::string text = "step = " + std::to_string(step) + "\n";
    if (!archive.empty()) text += "archive = " + archive + "\n";
    return write_safely(files, marker_path(cache), Bytes(text.begin(), text.end())) == FileStatus::Ok;
}

// The files under a folder, by their paths inside it, sorted; links and special files are not followed
// and are counted in `odd`. False if a folder could not be listed.
bool walk(Files* files, const std::string& folder, const std::string& inside, std::vector<std::string>* out, int* odd) {
    std::vector<FileInfo> entries;
    if (files->list(folder, &entries) != FileStatus::Ok) return false;
    std::sort(entries.begin(), entries.end(), [](const FileInfo& a, const FileInfo& b) { return a.name < b.name; });
    for (const FileInfo& entry : entries) {
        const std::string name = inside.empty() ? entry.name : inside + "/" + entry.name;
        if (entry.is_link || entry.is_special) {
            ++*odd;
        } else if (entry.is_folder) {
            if (!walk(files, folder + "/" + entry.name, name, out, odd)) return false;
        } else {
            out->push_back(name);
        }
    }
    return true;
}

// Whether a file unpacked at `name` under the staging folder's C64Music is moved into the collection
// (SPEC-149, FIND-026): a path HVSC's rules allow (SPEC-154), or one of HVSC's documents, a file under
// DOCUMENTS named with the same characters. No part may begin with a dot. Anything else stays where it was
// unpacked, and goes with the staging folder.
bool is_moved(const std::string& name) {
    const std::string path = "/" + name;
    if (path.find("/.") != std::string::npos) return false;
    if (is_hvsc_path(path)) return true;
    if (name.compare(0, 10, "DOCUMENTS/") != 0 || path.size() > 255) return false;
    for (const char c : name) {
        const bool allowed = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || is_digit(c) || c == '_' || c == '-' ||
                             c == '.' || c == '/';
        if (!allowed) return false;
    }
    return name.find("//") == std::string::npos && name.back() != '/';
}

}  // namespace

// --- SPEC-145 ------------------------------------------------------------------------------------------

DownloadOffer download_offer(const HvscStatus& status, Files* files, Unpacker* unpacker, Memory* memory,
                             const std::string& root) {
    DownloadOffer offer;
    offer.memory_mib = add_saturating(memory->available_mib(), memory->swap_free_mib());
    offer.unpacker = unpacker->available();
    std::uint64_t free_bytes = 0;
    if (files->free_bytes(root, &free_bytes) != FileStatus::Ok) free_bytes = 0;
    offer.free_mib = free_bytes / kMiB;
    offer.can_write = files->can_write(parent_folder(root));
    if (!status.set_up || status.release == 0) return offer;
    if (!offer.unpacker) {
        offer.text = "MSG_NEED_7ZIP";
    } else if (offer.memory_mib < kUnpackMemoryMib) {
        offer.text = "MSG_NEED_MEMORY";
        offer.about = words("n", offer.memory_mib);
    } else if (free_bytes < kDownloadRoomBytes) {
        offer.text = "MSG_NEED_ROOM";
        offer.about = {{"n", std::to_string(kDownloadRoomBytes / kMiB)}, {"m", std::to_string(offer.free_mib)}};
    } else if (!offer.can_write) {
        offer.text = "MSG_NEED_WRITE";
    } else {
        offer.offered = true;
    }
    return offer;
}

// --- SPEC-149 ------------------------------------------------------------------------------------------

bool read_7z_listing(const std::string& text, std::vector<ArchiveEntry>* out) {
    out->clear();
    bool in_entries = false;   // past the line of ten dashes that ends the archive's own block
    bool open = false;         // an entry's block has begun with its Path
    bool seven_z = false;      // the archive's own block says "Type = 7z", and no other type
    bool other_type = false;
    ArchiveEntry entry;
    const auto close = [&] {
        if (open) out->push_back(entry);
        entry = ArchiveEntry();
        open = false;
    };
    std::size_t at = 0;
    while (at <= text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!in_entries) {
            // 7-Zip lists other kinds of archive under any name (FIND-026): only a 7z archive is read.
            if (line.compare(0, 7, "Type = ") == 0) {
                if (line == "Type = 7z") seven_z = true;
                else other_type = true;
            }
            in_entries = line == "----------";
            if (in_entries && (!seven_z || other_type)) return false;
            continue;
        }
        if (line.empty()) {
            close();
            continue;
        }
        const std::size_t equals = line.find(" =");
        if (equals == std::string::npos) continue;
        const std::string key = line.substr(0, equals);
        std::string value = line.substr(equals + 2);
        if (!value.empty() && value[0] == ' ') value.erase(0, 1);
        if (key == "Path") {
            close();
            if (out->size() > kArchiveMaxEntries) return true;   // enough to refuse it; no need to read on
            open = true;
            entry.path = value;
        } else if (!open) {
            continue;
        } else if (key == "Size") {
            if (!parse_size(value, &entry.size)) return false;
        } else if (key == "Attributes") {
            read_attributes(value, &entry);
        } else if (key == "Mode") {
            // How 7-Zip gives the kind of an entry of other formats; a mode it cannot read is not known.
            if (is_unix_mode(value)) kind_of_mode(value[0], &entry);
            else if (!value.empty()) entry.is_special = true;
        } else if (key == "Folder") {
            if (value == "+") entry.is_folder = true;
            else if (value != "-" && !value.empty()) entry.is_special = true;
        } else if (key == "Symbolic Link" || key == "Hard Link") {
            if (!value.empty()) entry.is_link = true;
        } else if (key == "Device Major" || key == "Device Minor") {
            if (!value.empty()) entry.is_special = true;
        }
    }
    close();
    return in_entries;
}

ArchiveFault check_archive(const std::vector<ArchiveEntry>& entries) {
    if (entries.size() > kArchiveMaxEntries) return ArchiveFault::TooManyEntries;
    std::uint64_t total = 0;
    for (const ArchiveEntry& entry : entries) {
        if (entry.size > kArchiveMaxBytes - total) return ArchiveFault::TooLarge;
        total += entry.size;
    }
    for (const ArchiveEntry& entry : entries) {
        const std::string& path = entry.path;
        if (!path.empty() && path[0] == '/') return ArchiveFault::AbsolutePath;
        if (path.find('\\') != std::string::npos) return ArchiveFault::Backslash;
        std::size_t start = 0;
        bool first = true;
        for (;;) {
            const std::size_t slash = path.find('/', start);
            const std::string part = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
            if (part.empty()) return ArchiveFault::EmptyPart;
            if (part == "." || part == "..") return ArchiveFault::DotPart;
            if (first && part != kTopFolder) return ArchiveFault::OutsideTopFolder;
            if (first && slash == std::string::npos && !entry.is_folder) return ArchiveFault::OutsideTopFolder;
            first = false;
            if (slash == std::string::npos) break;
            start = slash + 1;
        }
        if (entry.is_link) return ArchiveFault::Link;
        if (entry.is_special) return ArchiveFault::Special;
    }
    return ArchiveFault::None;
}

// --- SPEC-146 ------------------------------------------------------------------------------------------

std::uint64_t room_for_unpacking(std::uint64_t archive_bytes, const std::vector<ArchiveEntry>& entries,
                                 std::uint64_t block_size) {
    if (block_size == 0) block_size = 1;
    std::uint64_t sum = archive_bytes;
    for (const ArchiveEntry& entry : entries) {
        if (entry.is_folder) continue;
        const std::uint64_t blocks = entry.size / block_size + (entry.size % block_size != 0 ? 1 : 0);
        sum = add_saturating(sum, blocks > UINT64_MAX / block_size ? UINT64_MAX : blocks * block_size);
    }
    return add_saturating(add_saturating(sum, sum / 10), 64 * kMiB);
}

// --- SPEC-144 ------------------------------------------------------------------------------------------

int read_install_marker(Files* files, const std::string& cache) { return read_marker(files, cache).step; }

bool write_install_marker(Files* files, const std::string& cache, int step) {
    return write_marker(files, cache, step, "");
}

struct CompleteDownload::Impl {
    CompleteDownloadParts p;
    std::atomic<bool> unfinished{false};
    std::atomic<bool> switched_off{false};   // the player was switched off for the running job
    std::string archive;                     // the archive's name in the cache, for the running job

    std::string staging() const {
        const std::string parent = parent_folder(p.root);
        return (parent == "/" ? "" : parent) + "/" + kStagingName;
    }
    std::string database() const { return p.root + "/" + kSongLengths; }
    std::string listing_file() const { return p.cache + "/listing.bin"; }
    std::string listing_old() const { return p.cache + "/listing.old"; }
    std::string listing_check() const { return p.cache + "/listing.check"; }
    bool stopped() const { return p.jobs->stop_asked(); }

    void mark(int step) {
        write_marker(p.files, p.cache, step, archive);
        p.jobs->progress(step, 0, 0);
    }

    // The end of the job, on the UI thread: playing on again, the notice, the listener.
    void end(bool done, int step, const std::string& notice = "", const std::map<std::string, std::string>& about = {}) {
        p.ui->post([this, done, step, notice, about] {
            if (switched_off.exchange(false) && p.play) p.play->playing_on();
            p.jobs->end();
            if (done) {
                if (p.notices) p.notices->raise("MSG_DOWNLOAD_DONE", {});
                if (p.listener) p.listener->download_done();
                return;
            }
            if (p.notices) {
                if (!notice.empty()) p.notices->raise(notice, about);
                else p.notices->raise("MSG_DOWNLOAD_FAILED", words("step", static_cast<std::uint64_t>(step)));
            }
            if (p.listener) p.listener->download_failed(step);
        });
    }

    // A failure or a stop in steps 1 to 6: the root is as it was; the staging folder and the marker go;
    // a partial archive stays.
    void fail_early(int step, const std::string& notice = "", const std::map<std::string, std::string>& about = {}) {
        remove_tree(p.files, staging());
        p.files->remove(listing_check());
        write_marker(p.files, p.cache, 0, "");
        end(false, step, notice, about);
    }

    // A failure in step 7 or 8: the marker, the staging folder and listing.old stay, for "Finish the
    // download" or the next start.
    void fail_late(int step) {
        unfinished = true;
        end(false, step);
    }

    // Steps 1 and 2, on the long worker.
    void check_first() {
        p.jobs->progress(2, 0, 0);
        Listing listing;
        listing.open(p.files, listing_file(), database());
        const HvscStatus status = hvsc_status(p.files, listing, p.root);
        if (!status.set_up || status.release == 0) return fail_early(1);
        std::string url = p.settings.get().url_archive;
        const std::string release = std::to_string(status.release);
        for (std::size_t at = url.find("{release}"); at != std::string::npos; at = url.find("{release}", at + release.size())) {
            url.replace(at, 9, release);
        }
        archive = archive_name(url);
        mark(2);
        const DownloadOffer offer = download_offer(status, p.files, p.unpacker, p.memory, p.root);
        if (!offer.offered) {
            if (offer.text == "MSG_NEED_ROOM") return fail_early(2, "MSG_NEED_ROOM", offer.about);
            return fail_early(2);
        }
        if (stopped()) return fail_early(2);
        // Step 2 ends with the player stopped; the download follows once it is.
        p.ui->post([this, url] {
            if (p.play) p.play->playing_off();
            switched_off = true;
            p.long_worker->post([this, url] { download_and_unpack(url); });
        });
    }

    // Steps 3 to 6, on the long worker.
    void download_and_unpack(const std::string& url) {
        Files* files = p.files;
        if (stopped()) return fail_early(2);

        // 3: the archive, carried on from where an earlier attempt was cut.
        mark(3);
        const HttpResult result = download_resumable(files, p.http, url, p.cache, archive, kArchiveLimitBytes,
                                                     [this](std::uint64_t done, std::uint64_t total) {
                                                         p.jobs->progress(3, done, total);
                                                         return !stopped();
                                                     });
        if (result.failure != HttpFailure::None || stopped()) return fail_early(3);
        const std::string archive_file = p.cache + "/" + archive + kPartEnding;

        // 4: its listing, checked, and the room it needs.
        mark(4);
        std::vector<ArchiveEntry> entries;
        if (!p.unpacker->list(archive_file, &entries) || stopped()) return fail_early(4);
        if (check_archive(entries) != ArchiveFault::None) return fail_early(4);
        FileInfo info;
        if (files->info(archive_file, &info) != FileStatus::Ok) return fail_early(4);
        std::uint64_t block = 0;
        if (files->block_size(p.root, &block) != FileStatus::Ok || block == 0) block = 4096;
        const Room room = check_room(files, p.root, room_for_unpacking(info.size, entries, block));
        if (!room.enough) {
            return fail_early(4, "MSG_NEED_ROOM", {{"n", std::to_string(room.needed_mib)}, {"m", std::to_string(room.free_mib)}});
        }
        if (stopped()) return fail_early(4);

        // 5: unpacked into the staging folder, which is removed first.
        mark(5);
        std::uint64_t file_count = 0;
        for (const ArchiveEntry& entry : entries) file_count += entry.is_folder ? 0 : 1;
        if (remove_tree(files, staging()) != FileStatus::Ok) return fail_early(5);
        const bool unpacked = p.unpacker->unpack(archive_file, staging(), [this, file_count](std::uint64_t written) {
            p.jobs->progress(5, written, file_count);
            return !stopped();
        });
        if (!unpacked || stopped()) return fail_early(5);

        // 6: no link or special file, and a song length file that gives a listing.
        mark(6);
        std::vector<std::string> found;
        int odd = 0;
        if (!walk(files, staging(), "", &found, &odd) || odd > 0 || stopped()) return fail_early(6);
        const ListingBuild check =
            Listing::build(files, staging() + "/" + kTopFolder + "/" + kSongLengths, listing_check());
        files->remove(listing_check());
        if (!check.ok || stopped()) return fail_early(6);

        // From step 7 on the job cannot be stopped. A stop that came just before still counts.
        p.jobs->no_stopping();
        if (stopped()) return fail_early(6);
        move_and_finish(false);
    }

    // Steps 7 and 8, on the long worker: for the first time, or taken up again (`again`), when listing.old
    // already says which files were HVSC's.
    void move_and_finish(bool again) {
        Files* files = p.files;
        p.jobs->no_stopping();
        mark(7);

        // The listing in use is kept as listing.old, unless that was done before the run was cut short.
        FileInfo info;
        if (!again || files->info(listing_old(), &info) != FileStatus::Ok) {
            if (copy_safely(files, listing_file(), listing_old(), kListingFileLimit) != FileStatus::Ok) return fail_late(7);
        }

        // Every file of the staging folder's C64Music that HVSC's rules allow is renamed over its place,
        // DOCUMENTS last and the song length file last of all.
        const std::string from_folder = staging() + "/" + kTopFolder;
        std::vector<std::string> found;
        int odd = 0;
        FileInfo staged;
        if (files->info(from_folder, &staged) == FileStatus::Ok && !walk(files, from_folder, "", &found, &odd)) {
            return fail_late(7);
        }
        found.erase(std::remove_if(found.begin(), found.end(), [](const std::string& name) { return !is_moved(name); }),
                    found.end());
        const auto rank = [](const std::string& name) {
            if (name == kSongLengths) return 2;
            return name.compare(0, 10, "DOCUMENTS/") == 0 ? 1 : 0;
        };
        std::stable_sort(found.begin(), found.end(), [&](const std::string& a, const std::string& b) { return rank(a) < rank(b); });
        for (const std::string& name : found) {
            const std::string to = p.root + "/" + name;
            if (files->make_folders(parent_folder(to)) != FileStatus::Ok) return fail_late(7);
            if (files->rename(from_folder + "/" + name, to) != FileStatus::Ok) return fail_late(7);
        }
        if (!found.empty()) files->sync(p.root + "/DOCUMENTS");

        // The new listing, and each file that only the old release had, if it is still HVSC's file.
        if (!Listing::build(files, database(), listing_file()).ok) return fail_late(7);
        Listing old_listing, now;
        if (!old_listing.open(files, listing_old(), "") || !now.open(files, listing_file(), database())) return fail_late(7);
        const int count = old_listing.tune_count();
        for (int first = 0; first < count; first += 1024) {
            for (const ListedTune& tune : old_listing.tunes(first, 1024)) {
                ListedTune listed;
                if (now.by_path(tune.path, &listed)) continue;
                const std::string file = p.root + tune.path;
                Bytes bytes;
                if (files->read(file, kTuneSizeLimit, &bytes) != FileStatus::Ok) continue;   // absent, or not a tune
                if (md5_hex(bytes) != tune.md5) continue;                                     // the user's own now
                if (files->remove(file) != FileStatus::Ok) return fail_late(7);
            }
        }

        // 8: what was left behind goes.
        mark(8);
        if (archive.empty()) {
            // A marker that did not name the archive: any archive of HVSC's in the cache.
            std::vector<FileInfo> cached;
            if (files->list(p.cache, &cached) == FileStatus::Ok) {
                for (const FileInfo& entry : cached) {
                    const std::string& n = entry.name;
                    const std::string ending = std::string(".7z") + kPartEnding;
                    if (n.size() > ending.size() && n.compare(n.size() - ending.size(), ending.size(), ending) == 0) {
                        discard_partial(files, p.cache, n.substr(0, n.size() - std::string(kPartEnding).size()));
                    }
                }
            }
        } else {
            discard_partial(files, p.cache, archive);
        }
        if (remove_tree(files, staging()) != FileStatus::Ok) return fail_late(8);
        if (files->remove(listing_old()) != FileStatus::Ok) return fail_late(8);
        write_marker(files, p.cache, 0, "");
        unfinished = false;
        end(true, 0);
    }
};

CompleteDownload::CompleteDownload(const CompleteDownloadParts& parts) : impl_(new Impl) { impl_->p = parts; }
CompleteDownload::~CompleteDownload() = default;

bool CompleteDownload::start() {
    Impl* s = impl_.get();
    if (s->p.jobs->running() || s->unfinished) return false;
    s->p.gate->ask([s] {
        if (s->unfinished || !s->p.jobs->begin(JobKind::Download)) return;
        s->archive.clear();
        s->p.long_worker->post([s] { s->check_first(); });
    });
    return true;
}

void CompleteDownload::recover() {
    Impl* s = impl_.get();
    const Marker marker = read_marker(s->p.files, s->p.cache);
    if (marker.step == 0) return;
    if (marker.step <= 6) {
        remove_tree(s->p.files, s->staging());
        s->p.files->remove(s->listing_check());
        write_marker(s->p.files, s->p.cache, 0, "");
        return;
    }
    s->unfinished = true;
    if (!s->p.jobs->begin(JobKind::Download)) return;   // "Finish the download" is offered instead
    s->archive = marker.archive;
    s->switched_off = true;
    s->p.ui->post([s] {
        if (s->p.play) s->p.play->playing_off();
    });
    s->move_and_finish(true);
}

bool CompleteDownload::unfinished() const { return impl_->unfinished; }

bool CompleteDownload::finish() {
    Impl* s = impl_.get();
    if (!s->unfinished || !s->p.jobs->begin(JobKind::Download)) return false;
    if (s->p.play) s->p.play->playing_off();
    s->switched_off = true;
    s->p.long_worker->post([s] {
        s->archive = read_marker(s->p.files, s->p.cache).archive;
        s->move_and_finish(true);
    });
    return true;
}

}  // namespace spz
