// SPDX-License-Identifier: GPL-3.0-or-later
// Releases (SPEC-148): the check for a newer release, the update, and what the update does with the tunes
// that are on the device.
//
// What an update found changed is kept in the cache as changed.txt: one HVSC path to a line, the tunes to
// fetch again when next played. It is written through SPEC-132.
#include "spz/releases.h"

#include <atomic>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

#include "hvsc_files.h"
#include "spz/listing.h"
#include "spz/md5.h"
#include "spz/path_text.h"
#include "spz/paths.h"
#include "spz/safe_file.h"
#include "spz/tune.h"

namespace spz {

using namespace hvsc_detail;

namespace {

const std::size_t kChangedLimit = 16u * 1024 * 1024;

}  // namespace

struct Releases::Impl {
    ReleasesParts p;
    std::atomic<int> newer{0};

    std::mutex mutex;              // over what follows
    bool loaded = false;
    std::set<std::string> changed;

    std::string changed_file() const { return p.cache + "/changed.txt"; }

    void load() {
        if (loaded) return;
        loaded = true;
        Bytes bytes;
        if (p.files->read(changed_file(), kChangedLimit, &bytes) != FileStatus::Ok) return;
        std::string line;
        for (const std::uint8_t c : bytes) {
            if (c == '\n') {
                if (is_hvsc_path(line)) changed.insert(line);
                line.clear();
            } else if (c != '\r') {
                line += static_cast<char>(c);
            }
        }
        if (is_hvsc_path(line)) changed.insert(line);
    }

    void save() {
        if (changed.empty()) {
            p.files->remove(changed_file());
            return;
        }
        std::string text;
        for (const std::string& path : changed) text += path + "\n";
        safe_write(p.files, changed_file(), Bytes(text.begin(), text.end()), FolderFlush::Ignored);
    }

    // SPEC-148: after the new documents and listing are in place, for each tune the old listing named
    // that is on the device.
    void sort_out_tunes(const Listing& old_listing) {
        Listing now;
        if (!now.open(p.files, p.cache + "/listing.bin", "")) return;
        std::lock_guard<std::mutex> lock(mutex);
        load();
        // What was remembered from an earlier update and the new listing no longer has at all is forgotten.
        for (auto it = changed.begin(); it != changed.end();) {
            ListedTune listed;
            it = now.by_path(*it, &listed) ? std::next(it) : changed.erase(it);
        }
        const int count = old_listing.tune_count();
        const int batch = 1024;
        for (int first = 0; first < count; first += batch) {
            for (const ListedTune& old_tune : old_listing.tunes(first, batch)) {
                const std::string file = p.root + old_tune.path;
                Bytes bytes;
                if (p.files->read(file, kTuneSizeLimit, &bytes) != FileStatus::Ok) continue;   // absent
                const std::string md5 = md5_hex(bytes);
                ListedTune listed;
                if (now.by_path(old_tune.path, &listed)) {
                    // The path is still HVSC's. If its bytes changed in HVSC and the file is still HVSC's
                    // old one, it is fetched again when next played.
                    if (listed.md5 != md5 && md5 == old_tune.md5) changed.insert(old_tune.path);
                    continue;
                }
                // The path is gone. If HVSC has these bytes at another path, the file is moved there.
                ListedTune elsewhere;
                if (!now.by_md5(md5, &elsewhere) || elsewhere.path == old_tune.path || !is_hvsc_path(elsewhere.path)) {
                    continue;   // a tune of the user's own from now on
                }
                const std::string to = p.root + elsewhere.path;
                FileInfo info;
                if (p.files->info(to, &info) == FileStatus::Ok) continue;   // never over a file that is there
                if (p.files->make_folders(path_text::folder_of(to)) != FileStatus::Ok) continue;
                if (p.files->rename(file, to) == FileStatus::Ok) p.files->sync(path_text::folder_of(to));
            }
        }
        save();
    }
};

Releases::Releases(const ReleasesParts& parts) : impl_(new Impl) { impl_->p = parts; }
Releases::~Releases() = default;

void Releases::check() {
    Impl* s = impl_.get();
    s->p.gate->ask([s] {
        s->p.long_worker->post([s] {
            const ReleasesParts& p = s->p;
            const std::string download = p.cache + "/hv_sids.check";
            HttpRequest request;
            request.url = join_url(p.settings.get().url_files, std::string("/") + kHvSids);
            request.to_path = download;
            request.limit_bytes = kDocumentLimitBytes;
            const HttpResult result = p.http->fetch(request, nullptr);
            int offered = 0, have = 0;
            Bytes bytes;
            if (result.failure == HttpFailure::None && p.files->read(download, kHvSidsReadLimit, &bytes) == FileStatus::Ok) {
                offered = read_release(bytes);
            }
            p.files->remove(download);
            bytes.clear();
            if (p.files->read(p.root + "/" + kHvSids, kHvSidsReadLimit, &bytes) == FileStatus::Ok) have = read_release(bytes);
            p.ui->post([s, offered, have] {
                NoticeSink* notices = s->p.notices;
                if (offered == 0) {
                    if (notices) notices->raise("MSG_CHECK_FAILED", {});
                } else if (offered > have) {
                    s->newer = offered;
                    if (notices) notices->raise("MSG_HVSC_NEWER", words("m", static_cast<std::uint64_t>(offered)));
                } else {
                    s->newer = 0;
                    if (notices) notices->raise("MSG_HVSC_NEWEST", words("n", static_cast<std::uint64_t>(have)));
                }
            });
        });
    });
}

int Releases::newer() const { return impl_->newer; }

bool Releases::update() {
    Impl* s = impl_.get();
    if (s->p.jobs->running()) return false;
    s->p.gate->ask([s] {
        if (!s->p.jobs->begin(JobKind::Update)) return;
        s->p.long_worker->post([s] {
            const ReleasesParts& p = s->p;
            // The listing in use, kept open: its mapping stays as it is when listing.bin is replaced.
            Listing old_listing;
            old_listing.open(p.files, p.cache + "/listing.bin", p.root + "/" + kSongLengths);
            const SetupOutcome outcome = run_setup_steps(p.files, p.http, p.jobs, p.settings.get().url_files, p.root, p.cache);
            if (outcome.failed_step == 0) {
                if (old_listing.is_open()) s->sort_out_tunes(old_listing);
                s->newer = 0;
            }
            p.ui->post([s, outcome] { tell_setup_outcome(outcome, s->p.jobs, s->p.notices, s->p.listener); });
        });
    });
    return true;
}

bool Releases::fetch_again(const std::string& path) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->load();
    return impl_->changed.count(path) > 0;
}

void Releases::fetched_again(const std::string& path) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->load();
    if (impl_->changed.erase(path) > 0) impl_->save();
}

}  // namespace spz
