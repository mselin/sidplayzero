// SPDX-License-Identifier: GPL-3.0-or-later
// Setting HVSC up (SPEC-140), the reader of hv_sids.txt (SPEC-148), the room on the card (SPEC-146), and
// the helpers of hvsc_files.h that the area's other files share.
#include "spz/hvsc_setup.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "hvsc_files.h"
#include "spz/listing.h"
#include "spz/path_text.h"
#include "spz/paths.h"
#include "spz/safe_file.h"

namespace spz {

namespace hvsc_detail {

const char* const kDocumentNames[5] = {"DOCUMENTS/Songlengths.md5", "DOCUMENTS/STIL.txt", "DOCUMENTS/HVSC.txt",
                                       "DOCUMENTS/BUGlist.txt", "DOCUMENTS/hv_sids.txt"};

namespace {

using path_text::last_part;

const std::uint64_t kMiB = 1024ull * 1024ull;

bool exists(Files* files, const std::string& path) {
    FileInfo info;
    return files->info(path, &info) == FileStatus::Ok;
}

// The documents in the order they are moved into the root: the song length file last (SPEC-140, step 5),
// since the root counts as set up once it is there.
std::vector<std::string> move_order() {
    std::vector<std::string> order;
    for (const char* name : kDocumentNames) {
        if (std::string(name) != kSongLengths) order.push_back(name);
    }
    order.push_back(kSongLengths);
    return order;
}

// Step 5 and step 6 of SPEC-140, undone when either fails, so that what was in the root before is there
// again: each document that was there is first copied aside, and each one moved in is taken out again.
class Placing {
public:
    Placing(Files* files, const std::string& root, const std::string& work) : files_(files), root_(root), work_(work) {}

    // Step 5. False if a document could not be put in place; everything is undone then.
    bool place_documents() {
        const std::string documents = root_ + "/DOCUMENTS";
        root_was_there_ = exists(files_, root_);
        documents_were_there_ = exists(files_, documents);
        if (files_->make_folders(documents) != FileStatus::Ok) {
            undo();
            return false;
        }
        const std::string aside = work_ + "/aside";
        if (files_->make_folders(aside) != FileStatus::Ok) {
            undo();
            return false;
        }
        for (const std::string& name : move_order()) {
            const std::string target = root_ + "/" + name;
            FileInfo info;
            if (files_->info(target, &info) == FileStatus::Ok) {
                if (info.is_folder || info.is_link ||
                    copy_safely(files_, target, aside + "/" + last_part(name), kDocumentLimitBytes) != FileStatus::Ok) {
                    undo();
                    return false;
                }
                kept_aside_.push_back(name);
            }
        }
        for (const std::string& name : move_order()) {
            const std::string target = root_ + "/" + name;
            if (copy_safely(files_, work_ + "/" + last_part(name), target, kDocumentLimitBytes) != FileStatus::Ok) {
                undo();
                return false;
            }
            placed_.push_back(name);
        }
        return true;
    }

    // Puts back what was there before, as well as it can.
    void undo() {
        for (auto it = placed_.rbegin(); it != placed_.rend(); ++it) {
            const std::string target = root_ + "/" + *it;
            bool was_there = false;
            for (const std::string& name : kept_aside_) was_there = was_there || name == *it;
            if (was_there) copy_safely(files_, work_ + "/aside/" + last_part(*it), target, kDocumentLimitBytes);
            else files_->remove(target);
        }
        placed_.clear();
        if (!documents_were_there_) files_->remove(root_ + "/DOCUMENTS");   // only if it is empty
        if (!root_was_there_) files_->remove(root_);
    }

private:
    Files* files_;
    std::string root_, work_;
    bool root_was_there_ = true, documents_were_there_ = true;
    std::vector<std::string> kept_aside_, placed_;
};

}  // namespace

FileStatus copy_safely(Files* files, const std::string& from, const std::string& to, std::size_t limit) {
    Bytes bytes;
    const FileStatus status = files->read(from, limit, &bytes);
    if (status != FileStatus::Ok) return status;
    return safe_write(files, to, bytes, FolderFlush::Ignored);
}

FileStatus remove_tree(Files* files, const std::string& folder) {
    FileInfo info;
    const FileStatus there = files->info(folder, &info);
    if (there == FileStatus::NotFound) return FileStatus::Ok;
    if (there != FileStatus::Ok) return there;
    if (!info.is_folder || info.is_link) return files->remove(folder);
    std::vector<FileInfo> entries;
    if (files->list(folder, &entries) == FileStatus::Ok) {
        for (const FileInfo& entry : entries) {
            const std::string path = folder + "/" + entry.name;
            if (entry.is_folder && !entry.is_link) remove_tree(files, path);
            else files->remove(path);
        }
    }
    return files->remove(folder);
}

std::map<std::string, std::string> words(const std::string& name, std::uint64_t value) {
    return {{name, std::to_string(value)}};
}

SetupOutcome run_setup_steps(Files* files, HttpClient* http, Jobs* jobs, const std::string& url_files,
                             const std::string& root, const std::string& cache) {
    SetupOutcome outcome;
    const auto fail = [&](int step) {
        outcome.failed_step = step;
        outcome.stopped = jobs->stop_asked();
        return outcome;
    };

    // 2: room.
    jobs->progress(2, 0, 0);
    outcome.room = check_room(files, root, kSetupRoomBytes);
    if (!outcome.room.enough) return fail(2);
    if (jobs->stop_asked()) return fail(2);

    // 3: the five documents, each whole, into a new folder in the cache.
    jobs->progress(3, 0, 0);
    const std::string work = cache + "/hvsc-download";
    remove_tree(files, work);
    if (files->make_folders(work) != FileStatus::Ok) return fail(3);
    std::uint64_t before = 0;
    for (const char* name : kDocumentNames) {
        HttpRequest request;
        request.url = join_url(url_files, std::string("/") + name);
        request.to_path = work + "/" + last_part(name);
        request.limit_bytes = kDocumentLimitBytes;
        const HttpResult result = http->fetch(request, [jobs, before](std::uint64_t done, std::uint64_t total) {
            jobs->progress(3, before + done, total == 0 ? 0 : before + total);
            return !jobs->stop_asked();
        });
        if (result.failure != HttpFailure::None || jobs->stop_asked()) {
            remove_tree(files, work);
            return fail(3);
        }
        FileInfo info;
        if (files->info(request.to_path, &info) == FileStatus::Ok) before += info.size;
    }

    // 4: the song length file gives a listing with a tune in it, and hv_sids.txt names a release.
    jobs->progress(4, 0, 0);
    const ListingBuild check = Listing::build(files, work + "/" + last_part(kSongLengths), work + "/listing.check");
    Bytes hv_sids;
    if (files->read(work + "/" + last_part(kHvSids), kHvSidsReadLimit, &hv_sids) == FileStatus::Ok) {
        outcome.release = read_release(hv_sids);
    }
    if (!check.ok || check.tunes < 1 || outcome.release == 0 || jobs->stop_asked()) {
        remove_tree(files, work);
        return fail(4);
    }
    files->remove(work + "/listing.check");

    // 5: into DOCUMENTS under the root, each through SPEC-132, the song length file last.
    jobs->progress(5, 0, 0);
    Placing placing(files, root, work);
    if (!placing.place_documents()) {
        remove_tree(files, work);
        return fail(5);
    }

    // 6: the listing. From here on there is nothing to stop.
    jobs->progress(6, 0, 0);
    const ListingBuild built = Listing::build(files, root + "/" + kSongLengths, cache + "/listing.bin");
    if (!built.ok) {
        placing.undo();
        remove_tree(files, work);
        outcome.failed_step = 6;
        return outcome;
    }
    remove_tree(files, work);
    return outcome;
}

void tell_setup_outcome(const SetupOutcome& outcome, Jobs* jobs, NoticeSink* notices, HvscListener* listener) {
    jobs->end();
    if (outcome.failed_step == 0) {
        if (notices) notices->raise("MSG_HVSC_READY", words("n", static_cast<std::uint64_t>(outcome.release)));
        if (listener) listener->hvsc_ready(outcome.release);
        return;
    }
    if (notices) {
        if (outcome.failed_step == 2 && !outcome.room.enough) {
            notices->raise("MSG_NEED_ROOM", {{"n", std::to_string(outcome.room.needed_mib)},
                                             {"m", std::to_string(outcome.room.free_mib)}});
        } else {
            notices->raise("MSG_HVSC_FAILED", words("step", static_cast<std::uint64_t>(outcome.failed_step)));
        }
    }
    if (listener) listener->hvsc_failed(outcome.stopped ? 0 : outcome.failed_step);
}

}  // namespace hvsc_detail

using namespace hvsc_detail;

int read_release(const Bytes& hv_sids) {
    const std::string word = "Release";
    const auto is_letter = [](std::uint8_t c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
    const auto is_digit = [](std::uint8_t c) { return c >= '0' && c <= '9'; };
    const std::size_t size = hv_sids.size();
    for (std::size_t at = 0; at + word.size() <= size; ++at) {
        if (std::equal(word.begin(), word.end(), hv_sids.begin() + static_cast<std::ptrdiff_t>(at)) == false) continue;
        const std::size_t end = at + word.size();
        // The word itself: not part of a longer one, such as "PreRelease" or "Releases".
        if ((at > 0 && (is_letter(hv_sids[at - 1]) || is_digit(hv_sids[at - 1]))) || (end < size && is_letter(hv_sids[end]))) {
            continue;
        }
        std::size_t i = end;
        while (i < size && !is_digit(hv_sids[i])) ++i;
        if (i == size) return 0;
        long value = 0;
        int digits = 0;
        while (i < size && is_digit(hv_sids[i])) {
            if (++digits > 9) return 0;   // no release has such a number
            value = value * 10 + (hv_sids[i] - '0');
            ++i;
        }
        return static_cast<int>(value);
    }
    return 0;
}

HvscStatus hvsc_status(Files* files, const Listing& listing, const std::string& root) {
    HvscStatus status;
    FileInfo info;
    status.set_up = listing.is_open() && files->info(root + "/" + kSongLengths, &info) == FileStatus::Ok && !info.is_folder;
    Bytes hv_sids;
    if (files->read(root + "/" + kHvSids, kHvSidsReadLimit, &hv_sids) == FileStatus::Ok) status.release = read_release(hv_sids);
    return status;
}

Room check_room(Files* files, const std::string& destination, std::uint64_t needed_bytes) {
    Room room;
    std::uint64_t free_bytes = 0;
    if (files->free_bytes(destination, &free_bytes) != FileStatus::Ok) free_bytes = 0;
    room.needed_mib = needed_bytes / kMiB + (needed_bytes % kMiB != 0 ? 1 : 0);
    room.free_mib = free_bytes / kMiB;
    room.enough = free_bytes >= needed_bytes;
    return room;
}

struct HvscSetup::Impl {
    HvscSetupParts parts;
};

HvscSetup::HvscSetup(const HvscSetupParts& parts) : impl_(new Impl{parts}) {}
HvscSetup::~HvscSetup() = default;

bool HvscSetup::start() {
    const HvscSetupParts p = impl_->parts;
    if (p.jobs->running()) return false;
    p.gate->ask([p] {
        if (!p.jobs->begin(JobKind::Setup)) return;
        p.long_worker->post([p] {
            const SetupOutcome outcome = run_setup_steps(p.files, p.http, p.jobs, p.settings.get().url_files, p.root, p.cache);
            p.ui->post([p, outcome] { tell_setup_outcome(outcome, p.jobs, p.notices, p.listener); });
        });
    });
    return true;
}

}  // namespace spz
