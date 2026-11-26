// SPDX-License-Identifier: GPL-3.0-or-later
// spz_tool: the integration tests' way into the `platform` layer, one command for each real part
// (the real HttpClient, the real engine, the real unpacker, the real volume, the device's input set-up).
//
//   spz_tool COMMAND [ARGUMENT ...]
//
// Each test that uses a command says, where it uses it, what the command takes and what it prints. Every
// command prints `key = value` lines. A command that reaches a part of `core` that is not built yet says
// so and ends with status 70, and the test that asked fails with "not built yet".
//
//   spz_tool listing DATABASE [--path HVSC_PATH]                       tests/integration/test_hvsc.py, SPEC-142
//   spz_tool fetch URL TO [--limit BYTES] [--resume-from BYTE] [--ca FILE] [--release-rules]  test_hvsc.py, SPEC-150
//   spz_tool setup --root DIR --cache DIR --url-files URL [--ca FILE]                      test_hvsc.py, SPEC-140
//   spz_tool unpacker list ARCHIVE | unpacker unpack ARCHIVE INTO              test_hvsc.py, SPEC-007, SPEC-149
//   spz_tool complete-download ...                                                         test_hvsc.py, SPEC-144
//   spz_tool volume get | set N | toggle-mute                                              test_play.py, SPEC-076
//   spz_tool input-setup DEVICES                                                           test_keys.py, SPEC-110
//   spz_tool audio-guard clean | allocate | lock                                      test_structure.py, SPEC-022
//   spz_tool load-tunes [--cut] FILE...                                               test_bad_tunes.py, SPEC-203
//   spz_tool kept-files                                                                    test_keep.py, SPEC-133
#include <signal.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "lvgl.h"
#include "platform/audio_guard.h"
#include "platform/curl_http.h"
#include "platform/display.h"
#include "platform/input_setup.h"
#include "platform/miniaudio_sink.h"
#include "platform/real_clock.h"
#include "platform/real_engine.h"
#include "platform/real_files.h"
#include "platform/software_sound.h"
#include "platform/system_seams.h"
#include "platform/threads.h"
#include "spz/complete_download.h"
#include "spz/folders.h"
#include "spz/hvsc_setup.h"
#include "spz/job.h"
#include "spz/listing.h"
#include "spz/not_built.h"
#include "spz/notice_gate.h"
#include "spz/safe_file.h"
#include "spz/settings.h"
#include "spz/settings_file.h"
#include "spz/state_file.h"
#include "spz/tune.h"

namespace {

using spz::platform::RealFiles;

int not_built(int argc, char** argv) {
    std::printf("not built yet: spz_tool");
    for (int i = 1; i < argc; ++i) std::printf(" %s", argv[i]);
    std::printf("\n");
    return 70;
}

int usage(const char* text) {
    std::fprintf(stderr, "usage: spz_tool %s\n", text);
    return 2;
}

void say(const std::string& key, const std::string& value) {
    std::printf("%s = %s\n", key.c_str(), value.c_str());
    std::fflush(stdout);
}

// Options of the form --name VALUE, and a flag or two; the rest are the command's own words, in order.
struct Arguments {
    std::vector<std::string> words;
    std::map<std::string, std::string> values;
    std::vector<std::string> flags;

    bool flag(const std::string& name) const {
        for (const std::string& f : flags) {
            if (f == name) return true;
        }
        return false;
    }
    std::string value(const std::string& name, const std::string& otherwise = "") const {
        const auto found = values.find(name);
        return found == values.end() ? otherwise : found->second;
    }
};

bool parse(const std::vector<std::string>& args, const std::vector<std::string>& flag_names, Arguments* out) {
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& word = args[i];
        if (word.compare(0, 2, "--") != 0) {
            out->words.push_back(word);
            continue;
        }
        bool is_flag = false;
        for (const std::string& name : flag_names) is_flag = is_flag || name == word;
        if (is_flag) {
            out->flags.push_back(word);
        } else if (i + 1 < args.size()) {
            out->values[word] = args[++i];
        } else {
            std::fprintf(stderr, "spz_tool: %s wants a value\n", word.c_str());
            return false;
        }
    }
    return true;
}

std::string folder_of(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) return ".";
    return slash == 0 ? "/" : path.substr(0, slash);
}

std::string name_of(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// --- listing (SPEC-142) -----------------------------------------------------------------------------------

// Builds the listing of a database into a folder of its own, opens it, and says what it holds.
int listing(const std::vector<std::string>& args) {
    if (args.empty()) return usage("listing DATABASE [--path HVSC_PATH]");
    std::string path;
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--path" && i + 1 < args.size()) {
            path = args[++i];
        } else {
            std::fprintf(stderr, "spz_tool listing: what is %s?\n", args[i].c_str());
            return 2;
        }
    }
    char folder[] = "/tmp/spz-listing-XXXXXX";
    if (::mkdtemp(folder) == nullptr) {
        std::perror("spz_tool listing");
        return 1;
    }
    RealFiles files;
    const std::string file = std::string(folder) + "/listing.bin";
    const spz::ListingBuild built = spz::Listing::build(&files, args[0], file);
    spz::Listing listing;
    const bool ok = built.ok && listing.open(&files, file, args[0]);
    std::printf("ok = %s\n", ok ? "yes" : "no");
    std::printf("tunes = %d\nfolders = %d\nlengths = %d\nskipped = %d\n", built.tunes, built.folders, built.lengths,
                built.lines_skipped);
    spz::ListedTune tune;
    if (ok && !path.empty() && listing.by_path(path, &tune)) {
        std::string lengths;
        for (const spz::Millis ms : tune.lengths) lengths += (lengths.empty() ? "" : " ") + std::to_string(ms);
        std::printf("lengths_ms = %s\n", lengths.c_str());
    }
    files.remove(file);
    files.remove(folder);
    return 0;
}

// --- fetch (SPEC-150) -------------------------------------------------------------------------------------

const char* failure_name(spz::HttpFailure failure) {
    switch (failure) {
        case spz::HttpFailure::None: return "none";
        case spz::HttpFailure::NotFound: return "not_found";
        case spz::HttpFailure::Network: return "network";
        case spz::HttpFailure::Tls: return "tls";
        case spz::HttpFailure::TooLarge: return "too_large";
        case spz::HttpFailure::CannotWrite: return "cannot_write";
        case spz::HttpFailure::CardFull: return "card_full";
        case spz::HttpFailure::Cancelled: return "cancelled";
    }
    return "network";
}

spz::platform::HttpRules rules_from(const Arguments& a) {
    spz::platform::HttpRules rules;
    if (!a.flag("--release-rules")) {
        rules.loopback_http = true;
        rules.extra_authority = a.value("--ca");
    }
    return rules;
}

int fetch(const std::vector<std::string>& args) {
    Arguments a;
    if (!parse(args, {"--release-rules"}, &a) || a.words.size() != 2) {
        return usage("fetch URL TO [--limit BYTES] [--resume-from BYTE] [--ca FILE] [--release-rules]");
    }
    spz::platform::CurlHttpClient http(rules_from(a));
    spz::HttpRequest request;
    request.url = a.words[0];
    request.to_path = a.words[1];
    request.limit_bytes = std::strtoull(a.value("--limit", "0").c_str(), nullptr, 10);
    request.resume_from = std::strtoull(a.value("--resume-from", "0").c_str(), nullptr, 10);
    const spz::HttpResult result = http.fetch(request, nullptr);
    say("failure", failure_name(result.failure));
    say("status", std::to_string(result.status));
    say("bytes", std::to_string(result.bytes));
    say("resumed", result.resumed ? "yes" : "no");
    return 0;
}

// --- what the jobs of `core` need around them ---------------------------------------------------------------

class AcceptedNotice : public spz::NoticeRecord {
public:
    bool notice_accepted() const override { return true; }
    void accept_notice() override {}
};

class NoPanel : public spz::NoticePanel {
public:
    void open_notice_panel(const std::string&) override {}
};

class PrintedNotices : public spz::NoticeSink {
public:
    void raise(const std::string& id, const std::map<std::string, std::string>& about) override {
        std::string words = id;
        for (const auto& [key, value] : about) words += " " + key + "=" + value;
        say("notice", words);
    }
};

// The UI thread of the tool: the main thread, running what is posted to it and the clock's timers until
// `done` says so, or the time is up.
bool run_ui(spz::platform::UiQueue* ui, spz::platform::RealClock* clock, const std::function<bool()>& done,
            const std::function<void()>& each = nullptr, long seconds = 3600) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < until) {
        ui->run_pending();
        if (clock) clock->run_due();
        if (each) each();
        if (done()) return true;
        ui->wait(5);
    }
    return false;
}

// --- setup (SPEC-140) -------------------------------------------------------------------------------------

int setup(const std::vector<std::string>& args) {
    Arguments a;
    if (!parse(args, {}, &a) || !a.words.empty() || a.value("--root").empty() || a.value("--cache").empty() ||
        a.value("--url-files").empty()) {
        return usage("setup --root DIR --cache DIR --url-files URL [--ca FILE]");
    }
    RealFiles files;
    spz::platform::CurlHttpClient http(rules_from(a));
    spz::platform::RealClock clock;
    spz::platform::UiQueue ui;
    AcceptedNotice record;
    NoPanel panel;
    spz::NoticeGate gate(&record, &panel);
    spz::Jobs jobs;
    PrintedNotices notices;
    spz::Settings settings;
    settings.url_files = a.value("--url-files");

    struct Listener : spz::HvscListener {
        std::atomic<int> ended{0};
        int release = 0, step = 0;
        void hvsc_ready(int r) override { release = r; ended = 1; }
        void hvsc_failed(int s) override { step = s; ended = 2; }
    } listener;

    spz::platform::WorkerThread long_worker;
    spz::HvscSetupParts parts;
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
    spz::HvscSetup job(parts);
    bool ended = false;
    if (job.start()) ended = run_ui(&ui, &clock, [&] { return listener.ended.load() != 0; });
    long_worker.stop();
    ui.run_pending();
    if (ended && listener.ended == 1) {
        say("result", "ready");
        say("release", std::to_string(listener.release));
    } else {
        say("result", "failed");
        say("step", std::to_string(listener.step));
    }
    return 0;
}

// --- unpacker (SPEC-007, SPEC-149) ------------------------------------------------------------------------

const char* fault_name(spz::ArchiveFault fault) {
    switch (fault) {
        case spz::ArchiveFault::None: return "none";
        case spz::ArchiveFault::AbsolutePath: return "absolute_path";
        case spz::ArchiveFault::EmptyPart: return "empty_part";
        case spz::ArchiveFault::DotPart: return "dot_part";
        case spz::ArchiveFault::Backslash: return "backslash";
        case spz::ArchiveFault::Link: return "link";
        case spz::ArchiveFault::Special: return "special";
        case spz::ArchiveFault::OutsideTopFolder: return "outside_top_folder";
        case spz::ArchiveFault::TooManyEntries: return "too_many_entries";
        case spz::ArchiveFault::TooLarge: return "too_large";
    }
    return "none";
}

int unpacker(const std::vector<std::string>& args) {
    spz::platform::SevenZipUnpacker unpacker;
    if (args.size() == 2 && args[0] == "list") {
        say("available", unpacker.available() ? "yes" : "no");
        std::vector<spz::ArchiveEntry> entries;
        if (!unpacker.list(args[1], &entries)) {
            say("listed", "no");
            return 0;
        }
        say("listed", "yes");
        say("entries", std::to_string(entries.size()));
        for (const spz::ArchiveEntry& entry : entries) {
            const char* kind = entry.is_link ? "link" : entry.is_special ? "special" : entry.is_folder ? "folder" : "file";
            say("entry", std::string(kind) + " " + std::to_string(entry.size) + " " + entry.path);
        }
        say("fault", fault_name(spz::check_archive(entries)));
        return 0;
    }
    if (args.size() == 3 && args[0] == "unpack") {
        say("ok", unpacker.unpack(args[1], args[2], nullptr) ? "yes" : "no");
        return 0;
    }
    return usage("unpacker list ARCHIVE | unpacker unpack ARCHIVE INTO");
}

// --- complete-download (SPEC-144) -------------------------------------------------------------------------

// What the test asks of a run: to stop the job in a step, or to be killed in one.
struct Watch {
    spz::Jobs* jobs = nullptr;
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
    void written(spz::Files* files, const std::string& to) {
        if (die_in_step > 0 && to == marker) {
            std::vector<std::uint8_t> bytes;
            if (files->read(marker, 4096, &bytes) == spz::FileStatus::Ok) {
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

class WatchedFiles : public spz::Files {
public:
    WatchedFiles(spz::Files* real, Watch* watch) : real_(real), watch_(watch) {}
    spz::FileStatus read(const std::string& p, std::size_t l, std::vector<std::uint8_t>* o) override {
        watch_->look();
        return real_->read(p, l, o);
    }
    spz::FileStatus write(const std::string& p, const std::vector<std::uint8_t>& b) override {
        watch_->look();
        const spz::FileStatus s = real_->write(p, b);
        if (s == spz::FileStatus::Ok) watch_->written(real_, p);
        return s;
    }
    spz::FileStatus append(const std::string& p, const std::vector<std::uint8_t>& b) override {
        watch_->look();
        return real_->append(p, b);
    }
    spz::FileStatus map(const std::string& p, std::size_t l, spz::MappedFile* o) override {
        watch_->look();
        return real_->map(p, l, o);
    }
    spz::FileStatus sync(const std::string& p) override { return real_->sync(p); }
    spz::FileStatus rename(const std::string& f, const std::string& t) override {
        watch_->look();
        const spz::FileStatus s = real_->rename(f, t);
        if (s == spz::FileStatus::Ok) {
            watch_->written(real_, t);
            watch_->renamed(f);
        }
        return s;
    }
    spz::FileStatus remove(const std::string& p) override {
        watch_->look();
        return real_->remove(p);
    }
    spz::FileStatus make_folders(const std::string& p) override { return real_->make_folders(p); }
    spz::FileStatus list(const std::string& f, std::vector<spz::FileInfo>* o) override {
        watch_->look();
        return real_->list(f, o);
    }
    spz::FileStatus info(const std::string& p, spz::FileInfo* o) override {
        watch_->look();
        return real_->info(p, o);
    }
    spz::FileStatus free_bytes(const std::string& p, std::uint64_t* o) override { return real_->free_bytes(p, o); }
    spz::FileStatus block_size(const std::string& p, std::uint64_t* o) override { return real_->block_size(p, o); }
    bool can_write(const std::string& f) override { return real_->can_write(f); }

private:
    spz::Files* real_;
    Watch* watch_;
};

// The archive from a file of the test's, whatever URL is asked for: a fake HttpClient that honours a range.
class ArchiveFromFile : public spz::HttpClient {
public:
    ArchiveFromFile(std::string archive, Watch* watch) : archive_(std::move(archive)), watch_(watch) {}
    spz::HttpResult fetch(const spz::HttpRequest& request, const Progress& progress) override {
        watch_->look();
        spz::HttpResult result;
        RealFiles files;
        std::vector<std::uint8_t> bytes;
        if (files.read(archive_, static_cast<std::size_t>(-1), &bytes) != spz::FileStatus::Ok) {
            result.failure = spz::HttpFailure::NotFound;
            result.status = 404;
            return result;
        }
        if (request.limit_bytes > 0 && bytes.size() > request.limit_bytes) {
            result.failure = spz::HttpFailure::TooLarge;
            return result;
        }
        const std::uint64_t from = request.resume_from <= bytes.size() ? request.resume_from : 0;
        std::vector<std::uint8_t> rest(bytes.begin() + static_cast<std::ptrdiff_t>(from), bytes.end());
        const spz::FileStatus wrote = from > 0 ? files.append(request.to_path, rest) : files.write(request.to_path, rest);
        if (wrote != spz::FileStatus::Ok) {
            result.failure = wrote == spz::FileStatus::NoSpace ? spz::HttpFailure::CardFull : spz::HttpFailure::CannotWrite;
            return result;
        }
        result.status = from > 0 ? 206 : 200;
        result.resumed = from > 0;
        result.bytes = rest.size();
        result.total = bytes.size();
        result.etag = "\"spz-archive\"";
        if (progress && !progress(bytes.size(), bytes.size())) result.failure = spz::HttpFailure::Cancelled;
        watch_->look();
        return result;
    }

private:
    std::string archive_;
    Watch* watch_;
};

class WatchedHttp : public spz::HttpClient {
public:
    WatchedHttp(spz::HttpClient* real, Watch* watch) : real_(real), watch_(watch) {}
    spz::HttpResult fetch(const spz::HttpRequest& request, const Progress& progress) override {
        watch_->look();
        return real_->fetch(request, [&](std::uint64_t done, std::uint64_t total) {
            watch_->look();
            return progress ? progress(done, total) : true;
        });
    }

private:
    spz::HttpClient* real_;
    Watch* watch_;
};

class WatchedUnpacker : public spz::Unpacker {
public:
    WatchedUnpacker(spz::Unpacker* real, Watch* watch) : real_(real), watch_(watch) {}
    bool available() override { return real_->available(); }
    bool list(const std::string& archive, std::vector<spz::ArchiveEntry>* out) override {
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
    spz::Unpacker* real_;
    Watch* watch_;
};

class EnoughMemory : public spz::Memory {
public:
    std::uint64_t available_mib() override { return 4096; }
    std::uint64_t swap_free_mib() override { return 0; }
};

class NoPlayer : public spz::PlaySwitch {
public:
    void playing_off() override {}
    void playing_on() override {}
};

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
    spz::Jobs jobs;
    Watch watch;
    watch.jobs = &jobs;
    watch.marker = cache + "/install.state";
    watch.staging = folder_of(root) + "/.sidplayzero-staging";
    watch.stop_in_step = std::atoi(a.value("--stop-in-step", "0").c_str());
    watch.die_in_step = std::atoi(a.value("--die-in-step", "0").c_str());
    watch.die_after_moves = std::atoi(a.value("--die-after-moves", "0").c_str());
    WatchedFiles files(&real_files, &watch);

    spz::platform::CurlHttpClient real_http(rules_from(a));
    ArchiveFromFile archive_http(a.value("--archive"), &watch);
    WatchedHttp http(a.value("--archive").empty() ? static_cast<spz::HttpClient*>(&real_http) : &archive_http, &watch);
    spz::platform::SevenZipUnpacker real_unpacker;
    WatchedUnpacker unpacker(&real_unpacker, &watch);
    EnoughMemory memory;
    spz::platform::RealClock clock;
    spz::platform::UiQueue ui;
    AcceptedNotice record;
    NoPanel panel;
    spz::NoticeGate gate(&record, &panel);
    PrintedNotices notices;
    NoPlayer player;
    spz::Settings settings;
    settings.url_archive =
        a.value("--archive").empty() ? a.value("--url-archive") : "https://archive.invalid/HVSC_{release}-all-of-them.7z";

    struct Listener : spz::DownloadListener {
        std::atomic<int> ended{0};
        int step = 0;
        void download_done() override { ended = 1; }
        void download_failed(int s) override { step = s; ended = 2; }
    } listener;

    // HVSC is set up in the root; its listing is built in the cache if it is not there.
    const std::string listing_file = cache + "/listing.bin";
    const std::string database = root + "/DOCUMENTS/Songlengths.md5";
    spz::FileInfo there;
    if (!recovering && real_files.info(listing_file, &there) != spz::FileStatus::Ok) {
        spz::Listing::build(&real_files, database, listing_file);
    }

    spz::platform::WorkerThread long_worker;
    spz::CompleteDownloadParts parts;
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
    spz::CompleteDownload download(parts);

    if (recovering) {
        const int before = spz::read_install_marker(&real_files, cache);
        std::atomic<bool> recovered{false};
        long_worker.post([&] {
            download.recover();
            recovered = true;
        });
        run_ui(&ui, &clock, [&] { return recovered.load() && (before < 7 || listener.ended.load() != 0 || !jobs.running()); });
        long_worker.stop();
        ui.run_pending();
        const int after = spz::read_install_marker(&real_files, cache);
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
    ui.run_pending();
    if (ended && listener.ended == 1) {
        say("result", "done");
    } else {
        say("result", "failed");
        say("step", std::to_string(listener.step));
    }
    return 0;
}

// --- volume (SPEC-076) ------------------------------------------------------------------------------------

int volume(const std::vector<std::string>& args) {
    spz::platform::PactlVolume volume;
    if (args.size() == 1 && args[0] == "get") {
        int level = 0;
        bool muted = false;
        if (!volume.get(&level, &muted)) {
            say("ok", "no");
            return 0;
        }
        say("ok", "yes");
        say("level", std::to_string(level));
        say("muted", muted ? "yes" : "no");
        return 0;
    }
    if (args.size() == 2 && args[0] == "set") {
        say("ok", volume.set(std::atoi(args[1].c_str())) ? "yes" : "no");
        return 0;
    }
    if (args.size() == 1 && args[0] == "toggle-mute") {
        say("ok", volume.toggle_mute() ? "yes" : "no");
        return 0;
    }
    return usage("volume get | volume set N | volume toggle-mute");
}

// --- input-setup (SPEC-110, SPEC-111) ---------------------------------------------------------------------

void read_no_key(lv_indev_t*, lv_indev_data_t* data) { data->state = LV_INDEV_STATE_RELEASED; }

int input_setup(const std::vector<std::string>& args) {
    if (args.size() != 1) return usage("input-setup DEVICES");
    std::FILE* file = std::fopen(args[0].c_str(), "r");
    if (file == nullptr) {
        std::perror(args[0].c_str());
        return 1;
    }
    // One device to a line: its path, a tab, and the words of what it reports.
    std::vector<spz::platform::InputDevice> devices;
    char line[4096];
    while (std::fgets(line, sizeof line, file)) {
        std::string text = line;
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
        if (text.empty()) continue;
        const std::size_t tab = text.find('\t');
        spz::platform::InputDevice device;
        device.path = text.substr(0, tab);
        const std::string words = tab == std::string::npos ? "" : " " + text.substr(tab + 1) + " ";
        device.letters = words.find(" letters ") != std::string::npos;
        device.buttons = words.find(" buttons ") != std::string::npos;
        device.relative = words.find(" relative ") != std::string::npos;
        device.absolute = words.find(" absolute ") != std::string::npos;
        devices.push_back(device);
    }
    std::fclose(file);

    lv_init();
    std::string why;
    if (!spz::platform::open_display(spz::platform::DisplayKind::Memory, &why)) {
        std::fprintf(stderr, "spz_tool input-setup: %s\n", why.c_str());
        return 1;
    }
    const char* named = std::getenv("SIDPLAYZERO_KEYBOARD");
    const std::string keyboard = spz::platform::choose_keyboard(devices, named ? named : "");
    spz::platform::make_keypad(read_no_key, nullptr);
    int keypads = 0, pointers = 0, encoders = 0, buttons = 0;
    for (lv_indev_t* indev = lv_indev_get_next(nullptr); indev != nullptr; indev = lv_indev_get_next(indev)) {
        switch (lv_indev_get_type(indev)) {
            case LV_INDEV_TYPE_KEYPAD: ++keypads; break;
            case LV_INDEV_TYPE_POINTER: ++pointers; break;
            case LV_INDEV_TYPE_ENCODER: ++encoders; break;
            case LV_INDEV_TYPE_BUTTON: ++buttons; break;
            default: break;
        }
    }
    say("keyboard", keyboard);
    say("keypads", std::to_string(keypads));
    say("pointers", std::to_string(pointers));
    say("encoders", std::to_string(encoders));
    say("buttons", std::to_string(buttons));
    say("grabbed", "no");   // the keyboard is read beside the launcher, never taken from it (SPEC-111)
    return 0;
}

// --- audio-guard (SPEC-022) -------------------------------------------------------------------------------

// A fake engine: a ramp, as the unit tests' fake gives.
class RampEngine : public spz::Engine {
public:
    bool load(const std::vector<std::uint8_t>&, int, const spz::EngineSettings&, std::string*) override {
        at_ = 0;
        return true;
    }
    std::size_t render(std::int16_t* out, std::size_t frames) override {
        for (std::size_t i = 0; i < frames; ++i) {
            const auto value = static_cast<std::int16_t>((at_ + i) % 32000);
            out[2 * i] = value;
            out[2 * i + 1] = static_cast<std::int16_t>(-value);
        }
        at_ += frames;
        return frames;
    }
    int chips() const override { return 1; }
    void set_muted(int, int, bool) override {}
    void set_filter(int, bool) override {}
    bool registers(int, std::uint8_t out[kRegisters]) override {
        std::memset(out, 0, kRegisters);
        return true;
    }

private:
    std::uint64_t at_ = 0;
};

// The real sink, with the test's misdeed added to the callback it is given: inside the guard, as the
// backend's own callback is.
class MisbehavingSink : public spz::AudioSink {
public:
    MisbehavingSink(spz::AudioSink* real, std::string how) : real_(real), how_(std::move(how)) {}
    bool open(unsigned rate, Pull pull) override {
        return real_->open(rate, [this, pull](std::int16_t* out, std::size_t frames) {
            pull(out, frames);
            const int call = ++callbacks;
            if (call != 3) return;
            if (how_ == "allocate") {
                static void* (*volatile allocate)(std::size_t) = std::malloc;
                void* memory = allocate(64);
                std::free(memory);
            } else if (how_ == "lock") {
                static std::mutex mutex;
                mutex.lock();
                mutex.unlock();
            }
        });
    }
    void close() override { real_->close(); }
    void on_device_lost(std::function<void()> fn) override { real_->on_device_lost(std::move(fn)); }
    std::atomic<int> callbacks{0};

private:
    spz::AudioSink* real_;
    std::string how_;
};

int audio_guard(const std::vector<std::string>& args) {
    if (args.size() != 1 || (args[0] != "clean" && args[0] != "allocate" && args[0] != "lock")) {
        return usage("audio-guard clean | allocate | lock");
    }
    if (!spz::platform::audio_guard_built()) {
        say("guard", "not in this build: it is in a test build without a sanitizer");
        return 1;
    }
    RampEngine engine;
    spz::platform::MiniaudioSink real_sink(spz::platform::MiniaudioSink::Output::Null);
    MisbehavingSink sink(&real_sink, args[0]);
    spz::platform::RealClock clock;
    spz::platform::UiQueue ui;
    spz::platform::SoftwareSound sound(&engine, &sink, &clock, &ui);
    spz::SoundBackend* backend = sound.backend();
    spz::EngineSettings settings;
    std::string why;
    if (!backend->load(std::vector<std::uint8_t>(), 1, settings, &why)) {
        say("load", "failed " + why);
        return 1;
    }
    backend->start();
    sound.start_render_thread();
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    run_ui(&ui, &clock, [&] { return std::chrono::steady_clock::now() >= until; });
    sound.stop_render_thread();
    backend->stop();
    real_sink.close();
    ui.run_pending();
    say("callbacks", std::to_string(sink.callbacks.load()));
    return 0;
}

// --- load-tunes (SPEC-203) --------------------------------------------------------------------------------

// A sink that opens and is never pulled: the render is all that is wanted.
class QuietSink : public spz::AudioSink {
public:
    bool open(unsigned, Pull) override { return true; }
    void close() override {}
    void on_device_lost(std::function<void()>) override {}
};

// What the player does with a tune file: SPEC-203's check, then, if it passes, load() of the real software
// backend with the real engine, and 100 ms rendered. "played", "refused MSG_..." or "failed MSG_...".
std::string load_one(const std::vector<std::uint8_t>& bytes) {
    const std::string refused = spz::check_tune(bytes);
    if (!refused.empty()) return "refused " + refused;
    spz::TuneInfo info;
    spz::read_tune_info(bytes, &info);
    spz::platform::RealEngine engine;
    QuietSink sink;
    spz::platform::RealClock clock;
    spz::platform::UiQueue ui;
    std::string result = "played";
    {
        spz::platform::SoftwareSound sound(&engine, &sink, &clock, &ui);
        spz::SoundBackend* backend = sound.backend();
        spz::EngineSettings settings;   // sidlite at 48,000 Hz, the model and clock from the header
        std::string why;
        if (!backend->load(bytes, info.default_song > 0 ? info.default_song : 1, settings, &why)) {
            result = "failed " + (why.empty() ? std::string("MSG_ENGINE_REFUSED") : why);
        } else {
            backend->start();
            const std::size_t wanted = settings.sample_rate / 10;
            for (int turn = 0; turn < 10000 && sound.buffered() < wanted; ++turn) {
                if (!sound.render_step()) break;
                ui.run_pending();
            }
            backend->stop();
        }
        ui.run_pending();
    }
    return result;
}

int load_tunes(const std::vector<std::string>& args) {
    bool cut = false;
    std::vector<std::string> paths;
    for (const std::string& word : args) {
        if (word == "--cut") {
            cut = true;
        } else {
            paths.push_back(word);
        }
    }
    if (paths.empty()) return usage("load-tunes [--cut] FILE...");
    RealFiles files;
    int done = 0;
    for (const std::string& path : paths) {
        std::vector<std::uint8_t> bytes;
        const spz::FileStatus read = files.read(path, 16 * 1024 * 1024, &bytes);
        if (read != spz::FileStatus::Ok) {
            std::fprintf(stderr, "spz_tool load-tunes: cannot read %s\n", path.c_str());
            return 1;
        }
        if (!cut) {
            say(name_of(path), load_one(bytes));
        } else {
            int refused = 0, failed = 0, played = 0;
            for (std::size_t length = 0; length <= bytes.size(); ++length) {
                const std::string got = load_one(std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(length)));
                if (got == "played") {
                    ++played;
                } else if (got.compare(0, 7, "refused") == 0) {
                    ++refused;
                } else {
                    ++failed;
                }
            }
            say(name_of(path), std::to_string(bytes.size() + 1) + " lengths, " + std::to_string(refused) + " refused, " +
                                   std::to_string(failed) + " failed, " + std::to_string(played) + " played");
        }
        ++done;
    }
    say("engine_sanitizer", spz::platform::engine_sanitizer());
    say("done", std::to_string(done));
    return 0;
}

// --- kept-files (SPEC-132, SPEC-133) ----------------------------------------------------------------------

std::string environment(const char* name) {
    const char* value = std::getenv(name);
    return value ? value : "";
}

int count_temp_files(RealFiles* files, const std::string& folder, int depth = 0) {
    std::vector<spz::FileInfo> entries;
    if (depth > 8 || files->list(folder, &entries) != spz::FileStatus::Ok) return 0;
    int count = 0;
    for (const spz::FileInfo& entry : entries) {
        if (entry.is_folder && !entry.is_link) {
            count += count_temp_files(files, folder + "/" + entry.name, depth + 1);
        } else if (spz::is_temp_name(entry.name)) {
            ++count;
        }
    }
    return count;
}

int kept_files(const std::vector<std::string>& args) {
    if (!args.empty()) return usage("kept-files");
    spz::Environment env;
    env.home = environment("HOME");
    env.xdg_config_home = environment("XDG_CONFIG_HOME");
    env.xdg_data_home = environment("XDG_DATA_HOME");
    env.xdg_state_home = environment("XDG_STATE_HOME");
    env.xdg_cache_home = environment("XDG_CACHE_HOME");
    const spz::Folders folders = spz::app_folders(env);
    RealFiles files;
    const std::size_t limit = 64 * 1024 + 1;   // anything longer is a fault of SPEC-133 all the same

    std::vector<std::uint8_t> bytes;
    spz::FileStatus status = files.read(spz::settings_path(folders), limit, &bytes);
    if (status == spz::FileStatus::NotFound) {
        say("settings", "missing");
    } else if (status != spz::FileStatus::Ok) {
        say("settings", "fault");
    } else {
        const spz::SettingsRead read = spz::read_settings(bytes);
        say("settings", read.fault || !read.used ? "fault" : "ok");
    }
    bytes.clear();
    status = files.read(spz::state_path(folders), limit, &bytes);
    if (status == spz::FileStatus::NotFound) {
        say("state", "missing");
    } else if (status != spz::FileStatus::Ok) {
        say("state", "fault");
    } else {
        const spz::StateRead read = spz::read_state(bytes);
        say("state", read.fault || !read.used ? "fault" : "ok");
    }
    // The app's four folders (SPEC-134): the playlists' folder is in the data folder.
    int temp = 0;
    for (const std::string& folder : {folders.config, folder_of(folders.playlists), folders.state, folders.cache}) {
        temp += count_temp_files(&files, folder);
    }
    say("temp_files", std::to_string(temp));
    return 0;
}

int dispatch(const std::string& command, const std::vector<std::string>& args) {
    if (command == "listing") return listing(args);
    if (command == "fetch") return fetch(args);
    if (command == "setup") return setup(args);
    if (command == "unpacker") return unpacker(args);
    if (command == "complete-download") return complete_download(args);
    if (command == "volume") return volume(args);
    if (command == "input-setup") return input_setup(args);
    if (command == "audio-guard") return audio_guard(args);
    if (command == "load-tunes") return load_tunes(args);
    if (command == "kept-files") return kept_files(args);
    return -1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return not_built(argc, argv);
    const std::string command = argv[1];
    const std::vector<std::string> args(argv + 2, argv + argc);
    try {
        const int status = dispatch(command, args);
        std::fflush(stdout);
        return status < 0 ? not_built(argc, argv) : status;
    } catch (const spz::NotBuilt& e) {
        std::printf("%s\n", e.what());
        std::fflush(stdout);
        return 70;
    }
}
