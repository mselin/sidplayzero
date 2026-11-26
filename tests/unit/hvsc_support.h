// SPDX-License-Identifier: GPL-3.0-or-later
// What the tests of the HVSC area share (tests/unit/hvsc_*_test.cpp): a small world made of the fakes, the
// fixture collections served by the fake HttpClient, and recorders for the interfaces this area declares.
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "fakes.h"
#include "spz/complete_download.h"
#include "spz/fetcher.h"
#include "spz/hvsc_setup.h"
#include "spz/job.h"
#include "spz/listing.h"
#include "spz/notice_gate.h"
#include "spz/settings.h"
#include "support.h"

namespace spz::test::hvsc {

namespace fs = std::filesystem;

const std::uint64_t kMiB = 1024ull * 1024ull;

// The default addresses (SPEC-130).
const std::string kUrlFiles = "https://hvsc.c64.org/download/C64Music";
inline std::string archive_url(int release) {
    return "https://hvsc.brona.dk/HVSC/HVSC_" + std::to_string(release) + "-all-of-them.7z";
}

// The five documents of SPEC-140, as the specification names them.
const std::vector<std::string> kDocuments = {
    "DOCUMENTS/Songlengths.md5", "DOCUMENTS/STIL.txt", "DOCUMENTS/HVSC.txt", "DOCUMENTS/BUGlist.txt",
    "DOCUMENTS/hv_sids.txt",
};

// Tunes of the fixture collections (tools/make_fixtures.py), by their HVSC paths.
const std::string kCommando = "/MUSICIANS/H/Hubbard_Rob/Commando.sid";
const std::string kThreeSongs = "/MUSICIANS/T/Tester_Tom/Three_Songs.sid";
const std::string kChanged = "/MUSICIANS/T/Tester_Tom/Changed.sid";
const std::string kOldName = "/MUSICIANS/T/Tester_Tom/Old_Name.sid";     // release 84 only
const std::string kRenamed = "/MUSICIANS/T/Tester_Tom/Renamed.sid";      // release 85 only: the same bytes
// Every tune release 85 lists, in the listing's order (SPEC-042).
const std::vector<std::string> kTunes85 = {
    "/DEMOS/A-F/Clock_Demo.sid",
    "/GAMES/A-F/Either_Chip.sid",
    "/MUSICIANS/H/Hubbard_Rob/Commando.sid",
    "/MUSICIANS/H/Huelsbeck_Chris/Huelsbeck_Test.sid",
    "/MUSICIANS/T/Tester_Tom/Changed.sid",
    "/MUSICIANS/T/Tester_Tom/Needs_BASIC.sid",
    "/MUSICIANS/T/Tester_Tom/New_Chip.sid",
    "/MUSICIANS/T/Tester_Tom/Renamed.sid",
    "/MUSICIANS/T/Tester_Tom/Three_Chips_3SID.sid",
    "/MUSICIANS/T/Tester_Tom/Three_Songs.sid",
    "/MUSICIANS/T/Tester_Tom/Two_Chips_2SID.sid",
};

inline std::string collection(int release) { return "collections/" + std::to_string(release) + "/C64Music"; }

// SPEC-132: the name a file is written under before it is renamed to `target`.
inline std::string temporary_of(const std::string& target) {
    const fs::path path(target);
    return (path.parent_path() / ("." + path.filename().string() + ".tmp-sidplayzero")).string();
}

inline bool starts_with(const std::string& text, const std::string& start) {
    return text.compare(0, start.size(), start) == 0;
}

inline spz::Bytes read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return spz::Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

inline void write_file(const fs::path& path, const spz::Bytes& data) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

inline bool exists(const fs::path& path) { return fs::exists(fs::symlink_status(path)); }

// Every file under a folder, by its path inside it, with its bytes: what "compared file by file" compares.
// A link is recorded as "link"; a folder that is not there gives nothing.
inline std::map<std::string, spz::Bytes> snapshot(const fs::path& folder) {
    std::map<std::string, spz::Bytes> files;
    if (!fs::is_directory(folder)) return files;
    for (const auto& entry : fs::recursive_directory_iterator(folder)) {
        const std::string name = fs::relative(entry.path(), folder).generic_string();
        if (entry.is_symlink()) files[name] = spz::test::bytes("link");
        else if (entry.is_regular_file()) files[name] = read_file(entry.path());
    }
    return files;
}

// The names of what a folder holds directly, sorted; nothing if it is not there.
inline std::vector<std::string> names_in(const fs::path& folder) {
    std::vector<std::string> names;
    if (!fs::is_directory(folder)) return names;
    for (const auto& entry : fs::directory_iterator(folder)) names.push_back(entry.path().filename().string());
    std::sort(names.begin(), names.end());
    return names;
}

// The MD5 a fixture database gives for a path: read here by the test's own few lines, not by the listing.
inline std::string md5_in_database(const spz::Bytes& database, const std::string& path) {
    const std::string text(database.begin(), database.end());
    const std::size_t at = text.find("; " + path + "\r\n");
    if (at == std::string::npos) return "";
    const std::size_t line = text.find('\n', at) + 1;
    return text.substr(line, 32);
}
inline std::string fixture_md5(int release, const std::string& path) {
    return md5_in_database(fixture(collection(release) + "/DOCUMENTS/Songlengths.md5"), path);
}

// The fake server has every file of a fixture collection, under `url_files`.
inline void serve_collection(fake::HttpClient* http, int release, const std::string& url_files = kUrlFiles) {
    const fs::path tree = fixture_path(collection(release));
    for (const auto& entry : fs::recursive_directory_iterator(tree)) {
        if (!entry.is_regular_file()) continue;
        fake::HttpClient::Answer answer;
        answer.body = read_file(entry.path());
        http->answer(url_files + "/" + fs::relative(entry.path(), tree).generic_string(), answer);
    }
}

// The addresses in the fake's request log, in order.
inline std::vector<std::string> urls(const fake::HttpClient& http) {
    std::vector<std::string> list;
    for (const spz::HttpRequest& request : http.log) list.push_back(request.url);
    return list;
}

// Operations of the fake Files that change something, as "what path" or "rename from -> to".
inline std::vector<fake::Files::Operation> changes(const fake::Files& files) {
    std::vector<fake::Files::Operation> list;
    for (const auto& op : files.log) {
        if (op.what == "write" || op.what == "rename" || op.what == "remove" || op.what == "make_folders") list.push_back(op);
    }
    return list;
}

// --- recorders for the interfaces of this area ------------------------------------------------------

struct Record : spz::NoticeRecord {
    bool accepted = false;
    bool notice_accepted() const override { return accepted; }
    void accept_notice() override { accepted = true; }
};

struct Panel : spz::NoticePanel {
    std::vector<std::string> opened;     // the text of each panel that was opened
    void open_notice_panel(const std::string& text) override { opened.push_back(text); }
};

struct Notices : spz::NoticeSink {
    struct Raised {
        std::string id;
        std::map<std::string, std::string> about;
    };
    std::vector<Raised> raised;
    void raise(const std::string& id, const std::map<std::string, std::string>& about) override { raised.push_back({id, about}); }

    std::vector<std::string> ids() const {
        std::vector<std::string> list;
        for (const Raised& r : raised) list.push_back(r.id);
        return list;
    }
    int count(const std::string& id) const {
        return static_cast<int>(std::count_if(raised.begin(), raised.end(), [&](const Raised& r) { return r.id == id; }));
    }
    // What the last notice with this identifier was about: "" if there was none, or it had no such name.
    std::string about(const std::string& id, const std::string& name) const {
        for (auto it = raised.rbegin(); it != raised.rend(); ++it) {
            if (it->id == id) {
                const auto found = it->about.find(name);
                return found == it->about.end() ? "" : found->second;
            }
        }
        return "";
    }
};

struct HvscEvents : spz::HvscListener {
    std::vector<std::string> events;     // "ready 85", "failed 3"
    void hvsc_ready(int release) override { events.push_back("ready " + std::to_string(release)); }
    void hvsc_failed(int step) override { events.push_back("failed " + std::to_string(step)); }
    int count(const std::string& start) const {
        return static_cast<int>(std::count_if(events.begin(), events.end(), [&](const std::string& e) { return starts_with(e, start); }));
    }
};

struct Heard : spz::FetchListener {
    std::vector<std::string> events;     // "started PATH", "fetched PATH", "failed PATH MESSAGE", "cancelled PATH"
    std::vector<spz::FetchWhy> whys;     // for each "fetched" and "failed", how it was asked for
    void fetch_started(const std::string& path) override { events.push_back("started " + path); }
    void fetched(const std::string& path, spz::FetchWhy why) override { events.push_back("fetched " + path); whys.push_back(why); }
    void fetch_failed(const std::string& path, spz::FetchWhy why, const std::string& message) override {
        events.push_back("failed " + path + " " + message);
        whys.push_back(why);
    }
    void fetch_cancelled(const std::string& path) override { events.push_back("cancelled " + path); }
    int count(const std::string& start) const {
        return static_cast<int>(std::count_if(events.begin(), events.end(), [&](const std::string& e) { return starts_with(e, start); }));
    }
};

struct Arrivals : spz::NewTuneListener {
    std::vector<std::string> paths;
    void tune_arrived(const std::string& path) override { paths.push_back(path); }
};

// --- a small world ----------------------------------------------------------------------------------

// The fakes and the folders one test needs: a home with a collection root (not made yet) and a cache.
struct World {
    TempFolder temp;
    fake::Files files;
    fake::HttpClient http;
    fake::Clock clock;
    fake::Executor fetch_worker, long_worker, ui;
    spz::Settings settings;
    Record record;
    Panel panel;
    spz::NoticeGate gate{&record, &panel};
    spz::Jobs jobs;
    Notices notices;
    std::string root = temp.file("C64Music");
    std::string cache = temp.file("cache/sidplayzero");

    World() { fs::create_directories(cache); }

    std::string in_root(const std::string& hvsc_path) const { return root + hvsc_path; }
    std::string listing_file() const { return cache + "/listing.bin"; }
    std::string database() const { return root + "/DOCUMENTS/Songlengths.md5"; }

    // Runs the workers and the UI's queue until nothing is left anywhere.
    void run() {
        while (fetch_worker.run_all() + long_worker.run_all() + ui.run_all() > 0) {}
    }

    // Puts a fixture collection's documents in the root, as after a set-up, with none of its tunes.
    void put_documents(int release) {
        copy_fixture_tree(collection(release) + "/DOCUMENTS", fs::path(root) / "DOCUMENTS");
    }
    // Puts a whole fixture collection in the root: every tune present.
    void put_collection(int release) { copy_fixture_tree(collection(release), root); }
    // Puts one tune of a fixture collection at its place in the root.
    void put_tune(int release, const std::string& hvsc_path) {
        write_file(in_root(hvsc_path), fixture(collection(release) + hvsc_path));
    }
    // Builds the listing from the root's database into the cache and opens it.
    bool open_listing(spz::Listing* listing) {
        const spz::ListingBuild built = spz::Listing::build(&files, database(), listing_file());
        return built.ok && listing->open(&files, listing_file(), database());
    }
};

// The parts of this area's objects, taken from a world.
inline spz::HvscSetupParts setup_parts(World& w, spz::HvscListener* listener = nullptr) {
    spz::HvscSetupParts parts;
    parts.files = &w.files;
    parts.http = &w.http;
    parts.long_worker = &w.long_worker;
    parts.ui = &w.ui;
    parts.gate = &w.gate;
    parts.jobs = &w.jobs;
    parts.notices = &w.notices;
    parts.settings = &w.settings;
    parts.root = w.root;
    parts.cache = w.cache;
    parts.listener = listener;
    return parts;
}

inline spz::FetcherParts fetcher_parts(World& w, const spz::Listing* listing, spz::NewTuneListener* arrivals = nullptr) {
    spz::FetcherParts parts;
    parts.files = &w.files;
    parts.http = &w.http;
    parts.clock = &w.clock;
    parts.fetch_worker = &w.fetch_worker;
    parts.ui = &w.ui;
    parts.gate = &w.gate;
    parts.listing = listing;
    parts.settings = &w.settings;
    parts.root = w.root;
    parts.arrivals = arrivals;
    return parts;
}

// --- a worker on its own thread, for a request that is held open --------------------------------------

// Runs the jobs of an executor on another thread, as a worker does while the UI thread goes on. The fake
// HttpClient holds a `stall` answer back until it is released; when this object goes away it releases
// every address it was told of and waits for the thread, so that a failing test cannot hang. While the
// thread runs, the test leaves the UI's executor and the fake clock alone.
class HeldWorker {
public:
    HeldWorker(fake::HttpClient* http, std::vector<std::string> held) : http_(http), held_(std::move(held)) {}
    ~HeldWorker() { finish(); }
    HeldWorker(const HeldWorker&) = delete;
    HeldWorker& operator=(const HeldWorker&) = delete;

    // Starts the thread: it runs the executor's jobs until none is left.
    void start(fake::Executor* executor) {
        thread_ = std::thread([this, executor] {
            try {
                executor->run_all();
            } catch (...) {
                error_ = std::current_exception();
            }
            done_ = true;
        });
    }
    // Waits, for at most ten seconds of real time, until `count` requests are being held back.
    bool wait_for_stalled(int count = 1) {
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (http_->stalled() < count) {
            if (std::chrono::steady_clock::now() > limit) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }
    // Waits, for at most ten seconds of real time, for the jobs to end by themselves: the held request
    // was cancelled, say. If they do not, everything held is released so that they can, and the answer
    // is false.
    bool wait_for_end() {
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        bool by_itself = true;
        while (!done_) {
            if (std::chrono::steady_clock::now() > limit) {
                by_itself = false;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        release_and_join();
        return by_itself;
    }
    // Lets every held answer go, and waits for the jobs to end.
    void release_and_join() {
        for (const std::string& url : held_) http_->release(url);
        if (thread_.joinable()) thread_.join();
        if (error_) {
            const std::exception_ptr error = error_;
            error_ = nullptr;
            std::rethrow_exception(error);
        }
    }

private:
    void finish() {
        for (const std::string& url : held_) http_->release(url);
        if (thread_.joinable()) thread_.join();
    }
    fake::HttpClient* http_;
    std::vector<std::string> held_;
    std::thread thread_;
    std::exception_ptr error_;
    std::atomic<bool> done_{false};
};

}  // namespace spz::test::hvsc
