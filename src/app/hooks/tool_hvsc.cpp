// SPDX-License-Identifier: GPL-3.0-or-later
// spz_tool (tool.h): the commands that run the HVSC area's jobs as the app runs them, with the real parts:
// `setup` (SPEC-140), `complete-download` (SPEC-144) and `update` (SPEC-148, FIND-031).
#include <signal.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app/hooks/tool.h"
#include "platform/system_seams.h"
#include "spz/complete_download.h"
#include "spz/hvsc_setup.h"
#include "spz/job.h"
#include "spz/listing.h"
#include "spz/path_text.h"
#include "spz/releases.h"
#include "spz/settings.h"
#include "spz/state_file.h"

namespace spz::tool {

// --- setup (SPEC-140) -------------------------------------------------------------------------------------

int setup(const std::vector<std::string>& args) {
    Arguments a;
    if (!parse(args, {}, &a) || !a.words.empty() || a.value("--root").empty() || a.value("--cache").empty() ||
        a.value("--url-files").empty()) {
        return usage("setup --root DIR --cache DIR --url-files URL [--ca FILE]");
    }
    RealFiles files;
    platform::CurlHttpClient http(rules_from(a));
    platform::RealClock clock;
    app::UiQueue ui;
    AcceptedNotice record;
    NoPanel panel;
    NoticeGate gate(&record, &panel);
    Jobs jobs;
    PrintedNotices notices;
    Settings settings;
    settings.url_files = a.value("--url-files");

    struct Listener : HvscListener {
        std::atomic<int> ended{0};
        int release = 0, step = 0;
        void hvsc_ready(int r) override { release = r; ended = 1; }
        void hvsc_failed(int s) override { step = s; ended = 2; }
    } listener;

    app::Worker long_worker{"spz-long"};
    HvscSetupParts parts;
    parts.files = &files;
    parts.http = &http;
    parts.long_worker = &long_worker;
    parts.ui = &ui;
    parts.gate = &gate;
    parts.jobs = &jobs;
    parts.notices = &notices;
    parts.settings = &settings;
    parts.root = a.value("--root");
    parts.cache = a.value("--cache");
    parts.listener = &listener;
    HvscSetup job(parts);
    bool ended = false;
    if (job.start()) ended = run_ui(&ui, &clock, [&] { return listener.ended.load() != 0; });
    long_worker.stop();
    ui.run_all();
    if (ended && listener.ended == 1) {
        say("result", "ready");
        say("release", std::to_string(listener.release));
    } else {
        say("result", "failed");
        say("step", std::to_string(listener.step));
    }
    return 0;
}

// --- complete-download (SPEC-144) -------------------------------------------------------------------------

namespace {

// What the test asks of a run: to stop the job in a step, or to be killed in one.
struct Watch {
    Jobs* jobs = nullptr;
    std::string marker;         // the cache's install.state
    std::string staging;        // the staging folder, beside the root
    int stop_in_step = 0;
    int die_in_step = 0;
    int die_after_moves = 0;
    std::atomic<bool> stop_asked{false};
    std::atomic<int> moves{0};

    // Called before and after everything the job asks of the seams.
    void look() {
        if (stop_in_step > 0 && !stop_asked && jobs->state().step >= stop_in_step) {
            if (jobs->stop()) stop_asked = true;
        }
    }
    static void die() {
        std::fflush(stdout);
        ::kill(::getpid(), SIGKILL);
    }
    // After a file is written or renamed to `to`.
    void written(Files* files, const std::string& to) {
        if (die_in_step > 0 && to == marker) {
            std::vector<std::uint8_t> bytes;
            if (files->read(marker, 4096, &bytes) == FileStatus::Ok) {
                // The marker names its step: any number in it that is the step asked for.
                std::string number;
                for (std::size_t i = 0; i <= bytes.size(); ++i) {
                    const char c = i < bytes.size() ? static_cast<char>(bytes[i]) : ' ';
                    if (c >= '0' && c <= '9') {
                        number += c;
                    } else {
                        if (!number.empty() && std::atoi(number.c_str()) == die_in_step) die();
                        number.clear();
                    }
                }
            }
        }
    }
    void renamed(const std::string& from) {
        if (die_after_moves > 0 && from.compare(0, staging.size() + 1, staging + "/") == 0) {
            if (++moves >= die_after_moves) die();
        }
    }
};

class WatchedFiles : public Files {
public:
    WatchedFiles(Files* real, Watch* watch) : real_(real), watch_(watch) {}
    FileStatus read(const std::string& p, std::size_t l, std::vector<std::uint8_t>* o) override {
        watch_->look();
        return real_->read(p, l, o);
    }
    FileStatus write(const std::string& p, const std::vector<std::uint8_t>& b) override {
        watch_->look();
        const FileStatus s = real_->write(p, b);
        if (s == FileStatus::Ok) watch_->written(real_, p);
        return s;
    }
    FileStatus append(const std::string& p, const std::vector<std::uint8_t>& b) override {
        watch_->look();
        return real_->append(p, b);
    }
    FileStatus map(const std::string& p, std::size_t l, MappedFile* o) override {
        watch_->look();
        return real_->map(p, l, o);
    }
    FileStatus sync(const std::string& p) override { return real_->sync(p); }
    FileStatus rename(const std::string& f, const std::string& t) override {
        watch_->look();
        const FileStatus s = real_->rename(f, t);
        if (s == FileStatus::Ok) {
            watch_->written(real_, t);
            watch_->renamed(f);
        }
        return s;
    }
    FileStatus remove(const std::string& p) override {
        watch_->look();
        return real_->remove(p);
    }
    FileStatus make_folders(const std::string& p) override { return real_->make_folders(p); }
    FileStatus list(const std::string& f, std::vector<FileInfo>* o) override {
        watch_->look();
        return real_->list(f, o);
    }
    FileStatus info(const std::string& p, FileInfo* o) override {
        watch_->look();
        return real_->info(p, o);
    }
    FileStatus free_bytes(const std::string& p, std::uint64_t* o) override { return real_->free_bytes(p, o); }
    FileStatus block_size(const std::string& p, std::uint64_t* o) override { return real_->block_size(p, o); }
    bool can_write(const std::string& f) override { return real_->can_write(f); }

private:
    Files* real_;
    Watch* watch_;
};

// The archive from a file of the test's, whatever URL is asked for: a fake HttpClient that honours a range.
class ArchiveFromFile : public HttpClient {
public:
    ArchiveFromFile(std::string archive, Watch* watch) : archive_(std::move(archive)), watch_(watch) {}
    HttpResult fetch(const HttpRequest& request, const Progress& progress) override {
        watch_->look();
        HttpResult result;
        RealFiles files;
        std::vector<std::uint8_t> bytes;
        if (files.read(archive_, static_cast<std::size_t>(-1), &bytes) != FileStatus::Ok) {
            result.failure = HttpFailure::NotFound;
            result.status = 404;
            return result;
        }
        if (request.limit_bytes > 0 && bytes.size() > request.limit_bytes) {
            result.failure = HttpFailure::TooLarge;
            return result;
        }
        const std::uint64_t from = request.resume_from <= bytes.size() ? request.resume_from : 0;
        std::vector<std::uint8_t> rest(bytes.begin() + static_cast<std::ptrdiff_t>(from), bytes.end());
        const FileStatus wrote = from > 0 ? files.append(request.to_path, rest) : files.write(request.to_path, rest);
        if (wrote != FileStatus::Ok) {
            result.failure = wrote == FileStatus::NoSpace ? HttpFailure::CardFull : HttpFailure::CannotWrite;
            return result;
        }
        result.status = from > 0 ? 206 : 200;
        result.resumed = from > 0;
        result.bytes = rest.size();
        result.total = bytes.size();
        result.etag = "\"spz-archive\"";
        if (progress && !progress(bytes.size(), bytes.size())) result.failure = HttpFailure::Cancelled;
        watch_->look();
        return result;
    }

private:
    std::string archive_;
    Watch* watch_;
};

class WatchedHttp : public HttpClient {
public:
    WatchedHttp(HttpClient* real, Watch* watch) : real_(real), watch_(watch) {}
    HttpResult fetch(const HttpRequest& request, const Progress& progress) override {
        watch_->look();
        return real_->fetch(request, [&](std::uint64_t done, std::uint64_t total) {
            watch_->look();
            return progress ? progress(done, total) : true;
        });
    }

private:
    HttpClient* real_;
    Watch* watch_;
};

class WatchedUnpacker : public Unpacker {
public:
    WatchedUnpacker(Unpacker* real, Watch* watch) : real_(real), watch_(watch) {}
    bool available() override { return real_->available(); }
    bool list(const std::string& archive, std::vector<ArchiveEntry>* out) override {
        watch_->look();
        const bool ok = real_->list(archive, out);
        watch_->look();
        return ok;
    }
    bool unpack(const std::string& archive, const std::string& into, const Progress& progress) override {
        watch_->look();
        const bool ok = real_->unpack(archive, into, [&](std::uint64_t files) {
            watch_->look();
            return progress ? progress(files) : true;
        });
        watch_->look();
        return ok;
    }

private:
    Unpacker* real_;
    Watch* watch_;
};

class EnoughMemory : public Memory {
public:
    std::uint64_t available_mib() override { return 4096; }
    std::uint64_t swap_free_mib() override { return 0; }
};

class NoPlayer : public PlaySwitch {
public:
    void playing_off() override {}
    void playing_on() override {}
};

}  // namespace

int complete_download(const std::vector<std::string>& args) {
    Arguments a;
    if (!parse(args, {"--recover"}, &a) || !a.words.empty() || a.value("--root").empty() || a.value("--cache").empty()) {
        return usage("complete-download --root DIR --cache DIR (--archive FILE | --url-archive URL [--ca FILE]) "
                     "[--stop-in-step N] [--die-in-step N] [--die-after-moves N] | --recover");
    }
    const std::string root = a.value("--root");
    const std::string cache = a.value("--cache");
    const bool recovering = a.flag("--recover");
    if (!recovering && a.value("--archive").empty() == a.value("--url-archive").empty()) {
        return usage("complete-download: one of --archive and --url-archive");
    }

    RealFiles real_files;
    Jobs jobs;
    Watch watch;
    watch.jobs = &jobs;
    watch.marker = cache + "/install.state";
    watch.staging = path_text::folder_of(root) + "/.sidplayzero-staging";
    watch.stop_in_step = std::atoi(a.value("--stop-in-step", "0").c_str());
    watch.die_in_step = std::atoi(a.value("--die-in-step", "0").c_str());
    watch.die_after_moves = std::atoi(a.value("--die-after-moves", "0").c_str());
    WatchedFiles files(&real_files, &watch);

    platform::CurlHttpClient real_http(rules_from(a));
    ArchiveFromFile archive_http(a.value("--archive"), &watch);
    WatchedHttp http(a.value("--archive").empty() ? static_cast<HttpClient*>(&real_http) : &archive_http, &watch);
    platform::SevenZipUnpacker real_unpacker;
    WatchedUnpacker unpacker(&real_unpacker, &watch);
    EnoughMemory memory;
    platform::RealClock clock;
    app::UiQueue ui;
    AcceptedNotice record;
    NoPanel panel;
    NoticeGate gate(&record, &panel);
    PrintedNotices notices;
    NoPlayer player;
    Settings settings;
    settings.url_archive =
        a.value("--archive").empty() ? a.value("--url-archive") : "https://archive.invalid/HVSC_{release}-all-of-them.7z";

    struct Listener : DownloadListener {
        std::atomic<int> ended{0};
        int step = 0;
        void download_done() override { ended = 1; }
        void download_failed(int s) override { step = s; ended = 2; }
    } listener;

    // HVSC is set up in the root; its listing is built in the cache if it is not there.
    const std::string listing_file = cache + "/listing.bin";
    const std::string database = root + "/DOCUMENTS/Songlengths.md5";
    FileInfo there;
    if (!recovering && real_files.info(listing_file, &there) != FileStatus::Ok) {
        Listing::build(&real_files, database, listing_file);
    }

    app::Worker long_worker{"spz-long"};
    CompleteDownloadParts parts;
    parts.files = &files;
    parts.http = &http;
    parts.unpacker = &unpacker;
    parts.memory = &memory;
    parts.long_worker = &long_worker;
    parts.ui = &ui;
    parts.gate = &gate;
    parts.jobs = &jobs;
    parts.notices = &notices;
    parts.settings = &settings;
    parts.root = root;
    parts.cache = cache;
    parts.play = &player;
    parts.listener = &listener;
    CompleteDownload download(parts);

    if (recovering) {
        const int before = read_install_marker(&real_files, cache);
        std::atomic<bool> recovered{false};
        long_worker.post([&] {
            download.recover();
            recovered = true;
        });
        run_ui(&ui, &clock, [&] { return recovered.load() && (before < 7 || listener.ended.load() != 0 || !jobs.running()); });
        long_worker.stop();
        ui.run_all();
        const int after = read_install_marker(&real_files, cache);
        if (before == 0) {
            say("result", "nothing");
        } else if (after != 0 || listener.ended == 2) {
            say("result", "failed");
        } else {
            say("result", before <= 6 ? "cleaned" : "done");
        }
        return 0;
    }

    bool ended = false;
    if (download.start()) {
        ended = run_ui(&ui, &clock, [&] { return listener.ended.load() != 0; }, [&] { watch.look(); });
    }
    long_worker.stop();
    ui.run_all();
    if (ended && listener.ended == 1) {
        say("result", "done");
    } else {
        say("result", "failed");
        say("step", std::to_string(listener.step));
    }
    return 0;
}

// --- update (SPEC-148, FIND-031) ----------------------------------------------------------------------------

namespace {

class ToldMemory : public Memory {
public:
    explicit ToldMemory(std::uint64_t mib) : mib_(mib) {}
    std::uint64_t available_mib() override { return mib_; }
    std::uint64_t swap_free_mib() override { return 0; }

private:
    std::uint64_t mib_;
};

// The notices printed, and the last one kept, so that the command knows when the check has answered.
class KeptNotices : public PrintedNotices {
public:
    void raise(const std::string& id, const std::map<std::string, std::string>& about) override {
        PrintedNotices::raise(id, about);
        std::lock_guard<std::mutex> lock(mutex_);
        last_ = id;
    }
    std::string last() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_;
    }

private:
    mutable std::mutex mutex_;
    std::string last_;
};

}  // namespace

int update(const std::vector<std::string>& args) {
    Arguments a;
    if (!parse(args, {}, &a) || !a.words.empty() || a.value("--root").empty() || a.value("--cache").empty() ||
        a.value("--state").empty() || a.value("--url-files").empty() || a.value("--archive").empty()) {
        return usage("update --root DIR --cache DIR --state FILE --url-files URL [--ca FILE] --archive FILE "
                     "[--memory-mib N]");
    }
    const std::string root = a.value("--root");
    const std::string cache = a.value("--cache");
    RealFiles files;
    platform::CurlHttpClient http(rules_from(a));
    Watch watch;   // nothing to watch: the archive's server only
    ArchiveFromFile archive_http(a.value("--archive"), &watch);
    platform::SevenZipUnpacker unpacker;
    ToldMemory memory(static_cast<std::uint64_t>(std::atoll(a.value("--memory-mib", "4096").c_str())));
    platform::RealClock clock;
    app::UiQueue ui;
    AcceptedNotice record;
    NoPanel panel;
    NoticeGate gate(&record, &panel);
    Jobs jobs;
    watch.jobs = &jobs;
    KeptNotices notices;
    NoPlayer player;
    Settings settings;
    settings.url_files = a.value("--url-files");
    settings.url_archive = "https://archive.invalid/HVSC_{release}-all-of-them.7z";

    struct Listener : HvscListener, DownloadListener {
        std::atomic<int> ended{0};   // 1 documents ready, 2 download done, 3 failed
        void hvsc_ready(int) override { ended = 1; }
        void hvsc_failed(int) override { ended = 3; }
        void download_done() override { ended = 2; }
        void download_failed(int) override { ended = 3; }
    } listener;

    // HVSC is set up in the root; its listing is built in the cache if it is not there.
    const std::string listing_file = cache + "/listing.bin";
    FileInfo there;
    if (files.info(listing_file, &there) != FileStatus::Ok) {
        Listing::build(&files, root + "/DOCUMENTS/Songlengths.md5", listing_file);
    }

    app::Worker long_worker{"spz-long"};
    app::Worker quick{"spz-quick"};
    StateFile state(&files, &clock, &quick, a.value("--state"));
    state.load();

    CompleteDownloadParts dparts;
    dparts.files = &files;
    dparts.http = &archive_http;
    dparts.unpacker = &unpacker;
    dparts.memory = &memory;
    dparts.long_worker = &long_worker;
    dparts.ui = &ui;
    dparts.gate = &gate;
    dparts.jobs = &jobs;
    dparts.notices = &notices;
    dparts.settings = &settings;
    dparts.root = root;
    dparts.cache = cache;
    dparts.play = &player;
    dparts.listener = &listener;
    dparts.state = &state;
    CompleteDownload download(dparts);

    ReleasesParts rparts;
    rparts.files = &files;
    rparts.http = &http;
    rparts.long_worker = &long_worker;
    rparts.ui = &ui;
    rparts.gate = &gate;
    rparts.jobs = &jobs;
    rparts.notices = &notices;
    rparts.settings = &settings;
    rparts.root = root;
    rparts.cache = cache;
    rparts.listener = &listener;
    rparts.state = &state;
    rparts.download = &download;
    rparts.unpacker = &unpacker;
    rparts.memory = &memory;
    Releases releases(rparts);

    // "Check for a newer release", then "Update to #m".
    releases.check();
    run_ui(&ui, &clock, [&] { return !notices.last().empty(); });
    say("newer", std::to_string(releases.newer()));
    bool ended = false;
    if (releases.newer() > 0 && releases.update()) {
        ended = run_ui(&ui, &clock, [&] { return listener.ended.load() != 0; });
    }
    long_worker.stop();
    ui.run_all();
    state.save_now();
    quick.stop();
    const int how = ended ? listener.ended.load() : 3;
    say("result", how == 1 ? "ready" : how == 2 ? "done" : "failed");
    say("complete_release", std::to_string(state.state().hvsc_complete_release));
    return 0;
}

}  // namespace spz::tool
