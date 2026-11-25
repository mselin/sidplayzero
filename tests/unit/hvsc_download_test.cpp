// SPDX-License-Identifier: GPL-3.0-or-later
// The complete download, part `download-all`: its steps and their order with the fake Unpacker (SPEC-007,
// SPEC-144), when it is offered (SPEC-145), the room it needs (SPEC-146), carrying on with a cut archive
// (SPEC-147), and the check of the archive's entries (SPEC-149). The real 7z is the integration level's.
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "hvsc_support.h"
#include "spz/complete_download.h"
#include "spz/hvsc_setup.h"
#include "spz/listing.h"
#include "support.h"

#if SPZ_PART_DOWNLOAD_ALL

using namespace spz::test::hvsc;
using spz::ArchiveEntry;
using spz::ArchiveFault;
using spz::test::bytes;
using spz::test::copy_fixture_tree;
using spz::test::fixture;

namespace {

using Words = std::vector<std::string>;

const char* const kOwnTune = "My Rips/Cool S\xC3\xB6ng.sid";

struct PlayRecorder : spz::PlaySwitch {
    Words log;
    std::function<void(const std::string&)> on;
    void playing_off() override { log.push_back("off"); if (on) on("playing off"); }
    void playing_on() override { log.push_back("on"); if (on) on("playing on"); }
};

struct DownloadEvents : spz::DownloadListener {
    Words events;     // "done", "failed 3"
    void download_done() override { events.push_back("done"); }
    void download_failed(int step) override { events.push_back("failed " + std::to_string(step)); }
};

// The fake Unpacker, with a way for a test to do something in the middle of a step.
struct WatchedUnpacker : spz::fake::Unpacker {
    std::function<void()> on_list, before_unpack;
    std::function<void(std::uint64_t)> on_file;
    std::function<void(const std::string& into)> after_unpack;

    bool list(const std::string& archive, std::vector<ArchiveEntry>* out) override {
        if (on_list) on_list();
        return spz::fake::Unpacker::list(archive, out);
    }
    bool unpack(const std::string& archive, const std::string& into, const Progress& progress) override {
        if (before_unpack) before_unpack();
        const bool ok = spz::fake::Unpacker::unpack(archive, into, [&](std::uint64_t files) {
            if (on_file) on_file(files);
            return !progress || progress(files);
        });
        if (after_unpack) after_unpack(into);
        return ok;
    }
};

// The fake HttpClient behind a door, for the same reason.
struct WatchedHttp : spz::HttpClient {
    spz::fake::HttpClient* inner = nullptr;
    std::function<void()> on_request;
    std::function<void(std::uint64_t, std::uint64_t)> on_progress;

    spz::HttpResult fetch(const spz::HttpRequest& request, const Progress& progress) override {
        if (on_request) on_request();
        return inner->fetch(request, [&](std::uint64_t done, std::uint64_t total) {
            if (on_progress) on_progress(done, total);
            return !progress || progress(done, total);
        });
    }
};

// What `7z l -slt` would list for a folder: the folders and files under it, by their paths inside it.
std::vector<ArchiveEntry> entries_of(const std::string& tree) {
    std::vector<ArchiveEntry> entries;
    for (const auto& item : fs::recursive_directory_iterator(tree)) {
        ArchiveEntry entry;
        entry.path = fs::relative(item.path(), tree).generic_string();
        entry.is_folder = item.is_directory();
        entry.size = item.is_regular_file() ? item.file_size() : 0;
        entries.push_back(entry);
    }
    return entries;
}

ArchiveEntry file_entry(const std::string& path, std::uint64_t size = 100) {
    ArchiveEntry entry;
    entry.path = path;
    entry.size = size;
    return entry;
}

// HVSC is set up with release 84 and every tune of it is on the device, with one tune of the user's own
// beside them. The archive that the fake server and the fake Unpacker give holds release 85: one tune
// renamed, one changed. The notice is accepted, and the device has the memory and the room.
struct Complete : World {
    WatchedUnpacker unpacker;
    spz::fake::Memory memory;
    WatchedHttp watched;
    PlayRecorder play;
    DownloadEvents events;
    spz::Listing listing;
    std::string staging = temp.file(".sidplayzero-staging");      // beside the collection root
    std::string tree = temp.file("archive");                      // what the fake Unpacker unpacks
    std::string marker = cache + "/install.state";
    std::string url = archive_url(84);                            // the release HVSC is set up with
    spz::Bytes archive;
    Words order;                                                   // what happened, each thing once, in order
    std::vector<int> steps;                                        // the job's step when each of those happened
    std::function<void(const spz::fake::Files::Operation&)> on_file_operation;
    std::map<std::string, spz::Bytes> before, expected;
    std::unique_ptr<spz::CompleteDownload> download;

    Complete() {
        record.accepted = true;
        put_collection(84);
        write_file(root + "/" + kOwnTune, fixture("tunes/own_tune.sid"));
        if (!open_listing(&listing)) throw std::runtime_error("the fixture database gave no listing");
        copy_fixture_tree(collection(85), fs::path(tree) / "C64Music");
        unpacker.tree = tree;
        unpacker.entries = entries_of(tree);
        files.set_free_bytes(4096 * kMiB);

        archive.resize(4000);
        for (std::size_t i = 0; i < archive.size(); ++i) archive[i] = static_cast<std::uint8_t>(i * 7);
        serve_archive();
        watched.inner = &http;

        before = snapshot(root);
        expected = snapshot(spz::test::fixture_path(collection(85)));
        expected[kOwnTune] = fixture("tunes/own_tune.sid");

        play.on = [this](const std::string& what) { note(what); };
        watched.on_request = [this] { note("download"); };
        unpacker.on_list = [this] { note("list"); };
        unpacker.before_unpack = [this] { note("unpack"); };
        files.before = [this](const spz::fake::Files::Operation& op) {
            const bool changes_it = op.what == "write" || op.what == "rename";
            if (changes_it && (op.path == cache + "/listing.old" || op.to == cache + "/listing.old")) note("listing.old");
            if (op.what == "rename" && starts_with(op.to, root + "/")) note("move");
            if (on_file_operation) on_file_operation(op);
        };
        download = make();
    }

    void serve_archive(std::uint64_t cut_after = 0) {
        spz::fake::HttpClient::Answer answer;
        answer.body = archive;
        answer.etag = "\"hvsc-archive\"";
        answer.last_modified = "Wed, 25 Nov 2026 10:00:00 GMT";
        answer.cut_after = cut_after;
        answer.steps = 4;
        http.answer(url, answer);
    }

    // A CompleteDownload over this world: the one the test uses, or the one the app makes after a restart.
    std::unique_ptr<spz::CompleteDownload> make() {
        spz::CompleteDownloadParts parts;
        parts.files = &files;
        parts.http = &watched;
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
        parts.play = &play;
        parts.listener = &events;
        return std::unique_ptr<spz::CompleteDownload>(new spz::CompleteDownload(parts));
    }

    void note(const std::string& what) {
        if (std::find(order.begin(), order.end(), what) != order.end()) return;
        order.push_back(what);
        steps.push_back(jobs.state().step);
    }
    int step_at(const std::string& what) const {
        const auto found = std::find(order.begin(), order.end(), what);
        return found == order.end() ? -1 : steps[static_cast<std::size_t>(found - order.begin())];
    }

    // The n-th rename that would put a file in the root fails (1 is the first), once.
    void fail_move(int n) {
        auto count = std::make_shared<int>(0);
        on_file_operation = [this, count, n](const spz::fake::Files::Operation& op) {
            if (op.what == "rename" && starts_with(op.to, root + "/") && ++*count == n) files.fail_at(1);
        };
    }

    // The files in the cache, by name, without the listing.
    Words cache_files() const {
        Words names;
        for (const auto& file : snapshot(cache)) {
            if (file.first != "listing.bin") names.push_back(file.first);
        }
        return names;
    }

    // What holds when a run has ended as an uninterrupted one does.
    void check_finished() {
        CHECK(snapshot(root) == expected);
        CHECK_FALSE(exists(staging));
        CHECK(cache_files().empty());                       // no archive, no listing.old, no marker
        CHECK(notices.count("MSG_DOWNLOAD_DONE") == 1);
        CHECK_FALSE(jobs.running());
        CHECK_FALSE(play.log.empty());
        if (!play.log.empty()) CHECK(play.log.back() == "on");
        spz::Listing now;
        REQUIRE(now.open(&files, listing_file(), database()));
        spz::ListedTune tune;
        CHECK(now.by_path(kRenamed, &tune));
        CHECK_FALSE(now.by_path(kOldName, &tune));
    }
};

}  // namespace

TEST_CASE("SPEC-007 SPEC-144 with the fake Unpacker the complete download runs its steps in order and ends with the archive's tree under the root [parts: download-all]") {
    Complete c;
    REQUIRE(c.download->start());
    CHECK(c.jobs.running());
    CHECK(c.jobs.state().kind == spz::JobKind::Download);
    CHECK(c.http.calls == 0);                          // the job is the long worker's
    c.run();

    // The steps, in the order of the specification: the player is stopped (2), the archive is downloaded
    // (3), listed (4) and unpacked (5), the listing in use is kept as listing.old and the files are moved (7).
    CHECK(c.order == Words{"playing off", "download", "list", "unpack", "listing.old", "move", "playing on"});
    CHECK(c.step_at("download") == 3);
    CHECK(c.step_at("list") == 4);
    CHECK(c.step_at("unpack") == 5);
    CHECK(c.step_at("move") == 7);

    // Step 3: the archive of the release HVSC is set up with, from url_archive, into the cache.
    REQUIRE(c.http.log.size() == 1);
    CHECK(c.http.log[0].url == "https://hvsc.brona.dk/HVSC/HVSC_84-all-of-them.7z");
    CHECK(c.http.log[0].limit_bytes == 512 * kMiB);
    CHECK(starts_with(c.http.log[0].to_path, c.cache + "/"));

    // Steps 4 and 5: the Unpacker lists that file, then unpacks it into the staging folder beside the root.
    REQUIRE(c.unpacker.log.size() == 2);
    CHECK(starts_with(c.unpacker.log[0], "list " + c.cache + "/"));
    const std::string archive_file = c.unpacker.log[0].substr(5);
    CHECK(c.unpacker.log[1] == "unpack " + archive_file + " " + c.staging);

    // Step 8, and the end: every tune present, the old release's own file gone, the user's file there.
    c.check_finished();
    for (const std::string& tune : kTunes85) CHECK(exists(c.in_root(tune)));
    CHECK_FALSE(exists(c.in_root(kOldName)));
    CHECK(c.notices.ids() == Words{"MSG_DOWNLOAD_DONE"});
    CHECK(c.events.events == Words{"done"});
    CHECK(c.play.log == Words{"off", "on"});
    CHECK(spz::read_install_marker(&c.files, c.cache) == 0);
}

TEST_CASE("SPEC-144 the staging folder is removed first if it exists, and nothing of it reaches the root [parts: download-all]") {
    Complete c;
    write_file(c.staging + "/C64Music/Left_Over.sid", fixture("tunes/own_tune.sid"));
    write_file(c.staging + "/junk.txt", bytes("from a download that was cut short long ago"));
    c.unpacker.before_unpack = [&] {
        CHECK_FALSE(exists(c.staging + "/junk.txt"));
        CHECK_FALSE(exists(c.staging + "/C64Music/Left_Over.sid"));
    };
    REQUIRE(c.download->start());
    c.run();
    c.check_finished();
}

TEST_CASE("SPEC-144 in step 7 the listing in use is copied to listing.old first, and the DOCUMENTS folder is moved last [parts: download-all]") {
    Complete c;
    const spz::Bytes listing_in_use = read_file(c.listing_file());
    spz::Bytes kept;
    Words moved;
    c.on_file_operation = [&](const spz::fake::Files::Operation& op) {
        if (op.what != "rename" || !starts_with(op.to, c.root + "/")) return;
        if (moved.empty()) kept = read_file(c.cache + "/listing.old");     // at the first move it is there already
        moved.push_back(op.to.substr(c.root.size() + 1));
        CHECK(starts_with(op.path, c.staging + "/C64Music/"));           // a rename out of the staging folder
        CHECK_FALSE(c.jobs.state().can_stop);                             // no stopping from here on
        CHECK_FALSE(c.jobs.stop());
    };
    REQUIRE(c.download->start());
    c.run();

    CHECK(kept == listing_in_use);
    REQUIRE(moved.size() == 16);                       // eleven tunes and five documents
    bool in_documents = false;
    for (const std::string& path : moved) {
        CAPTURE(path);
        if (starts_with(path, "DOCUMENTS/")) in_documents = true;
        else CHECK_FALSE(in_documents);                // nothing else follows the first document
    }
    CHECK(in_documents);
    c.check_finished();
}

TEST_CASE("SPEC-144 a file the user put in the root, or changed, is never removed by step 7 [parts: download-all]") {
    Complete c;
    // The user replaced a tune that only the old release lists with one of their own.
    write_file(c.in_root(kOldName), fixture("tunes/own_tune.sid"));
    c.expected[kOldName.substr(1)] = fixture("tunes/own_tune.sid");
    REQUIRE(c.download->start());
    c.run();
    c.check_finished();
    CHECK(read_file(c.root + "/" + kOwnTune) == fixture("tunes/own_tune.sid"));
    CHECK(read_file(c.in_root(kOldName)) == fixture("tunes/own_tune.sid"));
}

TEST_CASE("SPEC-144 stopping at each of steps 3 to 6 leaves the root's contents unchanged and no staging folder [parts: download-all]") {
    for (int step = 3; step <= 6; ++step) {
        CAPTURE(step);
        Complete c;
        bool asked = false;
        const auto stop = [&] {
            if (asked) return;
            asked = true;
            CHECK(c.jobs.state().step == step);
            CHECK(c.jobs.stop());                      // "Stop", confirmed
        };
        if (step == 3) c.watched.on_progress = [&](std::uint64_t, std::uint64_t) { stop(); };
        if (step == 4) c.unpacker.on_list = [&] { c.note("list"); stop(); };
        if (step == 5) c.unpacker.on_file = [&](std::uint64_t files) { if (files == 3) stop(); };
        if (step == 6) c.files.before = [&](const spz::fake::Files::Operation& op) {
            // The job is looking through what was unpacked.
            if (c.jobs.state().step == 6 && starts_with(op.path, c.staging)) stop();
        };

        REQUIRE(c.download->start());
        c.run();
        CHECK(asked);
        CHECK(snapshot(c.root) == c.before);           // compared file by file
        CHECK_FALSE(exists(c.staging));
        CHECK_FALSE(exists(c.marker));
        CHECK(c.notices.count("MSG_DOWNLOAD_FAILED") == 1);
        CHECK(c.notices.count("MSG_DOWNLOAD_DONE") == 0);
        CHECK(c.play.log == Words{"off", "on"});       // playing is switched on again
        CHECK_FALSE(c.jobs.running());
        CHECK_FALSE(c.download->unfinished());
        if (step == 3) {
            // A partial archive is kept, with its .meta, and nothing else.
            const Words left = c.cache_files();
            REQUIRE(left.size() == 2);
            CHECK(left[0].size() > 5);
            CHECK(left[0].substr(left[0].size() - 5) == ".part");
            CHECK(left[1] == left[0] + ".meta");
        }

        // The listing in use is still the old one.
        spz::Listing now;
        REQUIRE(now.open(&c.files, c.listing_file(), c.database()));
        spz::ListedTune tune;
        CHECK(now.by_path(kOldName, &tune));
    }
}

TEST_CASE("SPEC-144 SPEC-147 an archive download that was cut is carried on from the cut when the row is chosen again [parts: download-all]") {
    Complete c;
    c.serve_archive(2000);                             // the connection is lost half-way
    REQUIRE(c.download->start());
    c.run();
    CHECK(c.notices.count("MSG_DOWNLOAD_FAILED") == 1);
    CHECK(c.notices.about("MSG_DOWNLOAD_FAILED", "step") == "3");
    CHECK(c.events.events == Words{"failed 3"});
    CHECK(snapshot(c.root) == c.before);
    CHECK_FALSE(exists(c.staging));
    CHECK_FALSE(exists(c.marker));
    REQUIRE(c.cache_files().size() == 2);              // the partial archive and its .meta

    c.serve_archive();
    REQUIRE(c.download->start());
    c.run();
    REQUIRE(c.http.log.size() == 2);
    CHECK(c.http.log[1].resume_from == 2000);
    CHECK(c.http.log[1].if_etag == "\"hvsc-archive\"");
    CHECK(c.http.log[1].if_last_modified == "Wed, 25 Nov 2026 10:00:00 GMT");
    c.check_finished();
}

TEST_CASE("SPEC-144 a failure in any of steps 3 to 6 leaves the root as it was, removes the staging folder and the marker, and the notice names the step [parts: download-all]") {
    struct Fault {
        const char* what;
        int step;
        std::function<void(Complete*)> make;
    };
    const std::vector<Fault> faults = {
        {"the archive cannot be downloaded", 3, [](Complete* c) { c->http.down = true; }},
        {"the archive is not on the server", 3, [](Complete* c) {
             spz::fake::HttpClient::Answer gone;
             gone.status = 404;
             c->http.answer(c->url, gone);
         }},
        {"7z cannot list the archive", 4, [](Complete* c) {
             c->unpacker.on_list = [c] { c->note("list"); c->unpacker.is_available = false; };
         }},
        {"an entry of the archive would land outside the collection", 4, [](Complete* c) {
             c->unpacker.entries.push_back(file_entry("C64Music/../../outside.sid"));
         }},
        {"7z fails while it unpacks", 5, [](Complete* c) { c->unpacker.fail_after_files = 3; }},
        {"a link was unpacked", 6, [](Complete* c) {
             c->unpacker.after_unpack = [](const std::string& into) {
                 fs::create_symlink("/etc/hostname", into + "/C64Music/MUSICIANS/link.sid");
             };
         }},
        {"the unpacked song length file gives no listing", 6, [](Complete* c) {
             write_file(c->tree + "/C64Music/DOCUMENTS/Songlengths.md5", bytes("[Database]\r\n"));
         }},
        {"nothing was unpacked", 6, [](Complete* c) { c->unpacker.tree = ""; }},
    };
    for (const Fault& fault : faults) {
        CAPTURE(fault.what);
        Complete c;
        fault.make(&c);
        REQUIRE(c.download->start());
        c.run();
        CHECK(snapshot(c.root) == c.before);
        CHECK_FALSE(exists(c.staging));
        CHECK_FALSE(exists(c.marker));
        CHECK(c.notices.count("MSG_DOWNLOAD_FAILED") == 1);
        CHECK(c.notices.about("MSG_DOWNLOAD_FAILED", "step") == std::to_string(fault.step));
        CHECK(c.notices.count("MSG_DOWNLOAD_DONE") == 0);
        CHECK(c.events.events == Words{"failed " + std::to_string(fault.step)});
        CHECK(c.play.log == Words{"off", "on"});
        CHECK_FALSE(c.jobs.running());
        CHECK_FALSE(c.download->unfinished());
        if (fault.step == 4) CHECK(c.unpacker.log.size() == 1);      // nothing is unpacked
    }
}

TEST_CASE("SPEC-144 a rename made to fail in step 7 gives the notice and keeps the marker, the staging folder and listing.old; Finish the download then ends with the same tree [parts: download-all]") {
    for (int failing = 1; failing <= 16; ++failing) {
        CAPTURE(failing);
        Complete c;
        c.fail_move(failing);
        REQUIRE(c.download->start());
        c.run();

        CHECK(c.notices.count("MSG_DOWNLOAD_FAILED") == 1);
        CHECK(c.notices.about("MSG_DOWNLOAD_FAILED", "step") == "7");
        CHECK(c.events.events == Words{"failed 7"});
        CHECK(spz::read_install_marker(&c.files, c.cache) == 7);
        CHECK(exists(c.staging));
        CHECK(exists(c.cache + "/listing.old"));
        CHECK(c.play.log == Words{"off", "on"});       // playing is switched on again
        CHECK_FALSE(c.jobs.running());
        CHECK(c.download->unfinished());               // the HVSC screen offers "Finish the download"
        CHECK(exists(c.root + "/" + kOwnTune));

        // Finishing needs neither the network, nor 7z, nor the memory, nor the room: it only renames files.
        c.on_file_operation = nullptr;
        c.http.forbidden = true;
        c.unpacker.is_available = false;
        c.memory.available = 0;
        c.memory.swap_free = 0;
        c.files.set_free_bytes(1 * kMiB);
        const std::size_t unpacker_calls = c.unpacker.log.size();
        REQUIRE(c.download->finish());
        c.run();
        c.check_finished();
        CHECK_FALSE(c.download->unfinished());
        CHECK(c.http.forbidden_requests == 0);
        CHECK(c.unpacker.log.size() == unpacker_calls);
        CHECK(c.events.events == Words{"failed 7", "done"});
    }
}

TEST_CASE("SPEC-144 Finish the download is offered until it has succeeded [parts: download-all]") {
    Complete c;
    c.fail_move(5);
    REQUIRE(c.download->start());
    c.run();
    REQUIRE(c.download->unfinished());

    // The fault is still there: finishing fails again, and is still offered.
    c.fail_move(2);
    REQUIRE(c.download->finish());
    c.run();
    CHECK(c.notices.count("MSG_DOWNLOAD_FAILED") == 2);
    CHECK(c.download->unfinished());
    CHECK(spz::read_install_marker(&c.files, c.cache) == 7);

    c.on_file_operation = nullptr;
    REQUIRE(c.download->finish());
    c.run();
    c.check_finished();
    CHECK_FALSE(c.download->unfinished());
    CHECK_FALSE(c.download->finish());                 // nothing is left to finish
}

TEST_CASE("SPEC-144 a file that cannot be removed at the end of step 7 fails the step, and finishing removes it [parts: download-all]") {
    Complete c;
    c.on_file_operation = [&](const spz::fake::Files::Operation& op) {
        if (op.what == "remove" && op.path == c.in_root(kOldName)) c.files.fail_at(1);
    };
    REQUIRE(c.download->start());
    c.run();
    CHECK(c.notices.about("MSG_DOWNLOAD_FAILED", "step") == "7");
    CHECK(exists(c.in_root(kOldName)));
    CHECK(c.download->unfinished());

    c.on_file_operation = nullptr;
    REQUIRE(c.download->finish());
    c.run();
    c.check_finished();
}

TEST_CASE("SPEC-144 after being cut short while moving files, the next start takes steps 7 and 8 up again and ends with the same tree as an uninterrupted run [parts: download-all]") {
    for (const int cut_at : {1, 2, 7, 11, 12, 16}) {
        CAPTURE(cut_at);
        Complete c;
        c.fail_move(cut_at);
        REQUIRE(c.download->start());
        c.run();
        REQUIRE(spz::read_install_marker(&c.files, c.cache) == 7);
        c.on_file_operation = nullptr;

        // The app starts again: a new object finds the marker, the staging folder and listing.old.
        c.download.reset();
        std::unique_ptr<spz::CompleteDownload> restarted = c.make();
        c.http.forbidden = true;
        restarted->recover();                          // on the long worker, before anything reads the database
        c.run();
        c.check_finished();                            // the old release's own file gone, the user's file there
        CHECK_FALSE(restarted->unfinished());
        CHECK(c.http.forbidden_requests == 0);
    }
}

TEST_CASE("SPEC-144 at the next start a marker naming a step from 1 to 6 means the clean-up: the staging folder and the marker go, the root stays [parts: download-all]") {
    for (int step = 1; step <= 6; ++step) {
        CAPTURE(step);
        Complete c;
        // As the app was killed: the marker, part of an archive, and whatever had been unpacked so far.
        REQUIRE(spz::write_install_marker(&c.files, c.cache, step));
        REQUIRE(exists(c.marker));
        CHECK(spz::read_install_marker(&c.files, c.cache) == step);
        write_file(c.staging + "/C64Music/MUSICIANS/H/Hubbard_Rob/Commando.sid", fixture("tunes/commando_test.sid"));

        c.http.forbidden = true;
        c.download->recover();
        c.run();
        CHECK(snapshot(c.root) == c.before);
        CHECK_FALSE(exists(c.staging));
        CHECK_FALSE(exists(c.marker));
        CHECK_FALSE(c.download->unfinished());
        CHECK(c.http.forbidden_requests == 0);
        CHECK(c.unpacker.log.empty());
    }
    // With no marker there is nothing to do, and nothing is touched.
    Complete c;
    const auto cache_before = snapshot(c.cache);
    c.download->recover();
    c.run();
    CHECK(snapshot(c.root) == c.before);
    CHECK(snapshot(c.cache) == cache_before);
    CHECK_FALSE(c.download->unfinished());
    CHECK(c.notices.raised.empty());
}

TEST_CASE("SPEC-144 the marker names the step the download is in, and writing step 0 removes it [parts: download-all]") {
    spz::test::TempFolder temp;
    spz::fake::Files files;
    const std::string cache = temp.file("cache");
    fs::create_directories(cache);
    CHECK(spz::read_install_marker(&files, cache) == 0);
    for (int step = 1; step <= 8; ++step) {
        REQUIRE(spz::write_install_marker(&files, cache, step));
        CHECK(spz::read_install_marker(&files, cache) == step);
        CHECK(names_in(cache) == Words{"install.state"});
    }
    REQUIRE(spz::write_install_marker(&files, cache, 0));
    CHECK(names_in(cache).empty());
    // A marker that says nothing readable is no marker.
    write_file(cache + "/install.state", bytes("\xFF\xFE not a step"));
    CHECK(spz::read_install_marker(&files, cache) == 0);
}

TEST_CASE("SPEC-146 SPEC-144 with one MiB less than 700 MiB free no download request is made and the notice shows the two numbers; with exactly enough the download starts [parts: download-all]") {
    {
        Complete c;
        c.files.set_free_bytes(699 * kMiB);
        c.download->start();
        c.run();
        CHECK(c.http.calls == 0);
        CHECK(c.unpacker.log.empty());
        CHECK(c.notices.count("MSG_NEED_ROOM") == 1);
        CHECK(c.notices.about("MSG_NEED_ROOM", "n") == "700");
        CHECK(c.notices.about("MSG_NEED_ROOM", "m") == "699");
        CHECK(snapshot(c.root) == c.before);
        CHECK_FALSE(c.jobs.running());
        if (!c.play.log.empty()) CHECK(c.play.log.back() == "on");
    }
    {
        Complete c;
        c.files.set_free_bytes(700 * kMiB);
        REQUIRE(c.download->start());
        c.run();
        CHECK(c.http.log.size() == 1);
        CHECK(c.notices.count("MSG_NEED_ROOM") == 0);
        c.check_finished();
    }
}

TEST_CASE("SPEC-146 SPEC-144 once the archive is listed, the room it needs is its size, its entries, a tenth and 64 MiB: one MiB less and nothing is unpacked [parts: download-all]") {
    // The archive is 1 MiB, and its listing says it unpacks to 999 MiB: 1,000 MiB, a tenth more, and 64.
    for (const std::uint64_t free_mib : {1163, 1164}) {
        CAPTURE(free_mib);
        Complete c;
        c.archive.assign(1 * kMiB, 0x5A);
        c.serve_archive();
        // Sizes in whole MiB, so that the figure is the same whatever the file system's block size.
        ArchiveEntry top;
        top.path = "C64Music";
        top.is_folder = true;
        c.unpacker.entries = {top, file_entry("C64Music/Big_One.sid", 500 * kMiB), file_entry("C64Music/Big_Two.sid", 499 * kMiB)};
        c.files.set_free_bytes(free_mib * kMiB);

        REQUIRE(c.download->start());
        c.run();
        REQUIRE(c.http.log.size() == 1);
        if (free_mib == 1163) {
            CHECK(c.unpacker.log.size() == 1);         // listed, not unpacked
            CHECK(c.notices.count("MSG_NEED_ROOM") == 1);
            CHECK(c.notices.about("MSG_NEED_ROOM", "n") == "1164");
            CHECK(c.notices.about("MSG_NEED_ROOM", "m") == "1163");
            CHECK(snapshot(c.root) == c.before);
            CHECK_FALSE(exists(c.staging));
            CHECK_FALSE(exists(c.marker));
        } else {
            CHECK(c.unpacker.log.size() == 2);
            CHECK(c.notices.count("MSG_NEED_ROOM") == 0);
        }
    }
}

TEST_CASE("SPEC-146 the room for unpacking is the archive's size plus its entries, each rounded up to the block size, plus a tenth, plus 64 MiB [parts: download-all]") {
    // Ten entries of one byte each take ten blocks.
    std::vector<ArchiveEntry> entries;
    for (int i = 0; i < 10; ++i) entries.push_back(file_entry("C64Music/a" + std::to_string(i) + ".sid", 1));
    CHECK(spz::room_for_unpacking(40960, entries, 4096) == 40960 + 40960 + 8192 + 64 * kMiB);
    // Whole blocks are not rounded further, and a folder takes nothing.
    entries.clear();
    entries.push_back(file_entry("C64Music/whole.sid", 8192));
    entries.push_back(file_entry("C64Music/one_more.sid", 8193));
    ArchiveEntry folder;
    folder.path = "C64Music";
    folder.is_folder = true;
    entries.push_back(folder);
    CHECK(spz::room_for_unpacking(0, entries, 4096) == (8192 + 12288) + (8192 + 12288) / 10 + 64 * kMiB);
    // The figure the specification's 700 MiB stands in for: release 85's archive and tree.
    entries.clear();
    entries.push_back(file_entry("C64Music/all.sid", 400 * kMiB));
    CHECK(spz::room_for_unpacking(80 * kMiB, entries, 4096) == 480 * kMiB + 48 * kMiB + 64 * kMiB);
    CHECK(spz::room_for_unpacking(0, {}, 4096) == 64 * kMiB);
}

// --- SPEC-145 ------------------------------------------------------------------------------------------

namespace {

// A device on which every condition of SPEC-145 holds, each exactly: 450 MiB of memory with the swap
// counted in, a 7z, 700 MiB free, a folder that can be written, HVSC set up with a known release.
struct Offer {
    spz::test::TempFolder temp;
    spz::fake::Files files;
    spz::fake::Unpacker unpacker;
    spz::fake::Memory memory;
    spz::HvscStatus status;
    std::string root = temp.file("C64Music");

    Offer() {
        fs::create_directories(root);
        status.set_up = true;
        status.release = 85;
        memory.available = 250;
        memory.swap_free = 200;
        files.set_free_bytes(700 * kMiB);
    }
    spz::DownloadOffer ask() { return spz::download_offer(status, &files, &unpacker, &memory, root); }
};

}  // namespace

TEST_CASE("SPEC-145 with 450 MiB of memory and swap, a 7z, 700 MiB free and a folder that can be written, the complete download is offered [parts: download-all]") {
    Offer o;
    const spz::DownloadOffer offer = o.ask();
    CHECK(offer.offered);
    CHECK(offer.text == "");
    CHECK(offer.memory_mib == 450);
    CHECK(offer.unpacker);
    CHECK(offer.free_mib == 700);
    CHECK(offer.can_write);
    // It asked the seams (SPEC-021).
    CHECK(o.memory.calls >= 2);
    CHECK(o.unpacker.calls >= 1);
    CHECK(o.files.count("free_bytes") >= 1);
    CHECK(o.files.count("can_write") >= 1);

    // Memory and swap count together, in any share.
    o.memory.available = 450; o.memory.swap_free = 0;
    CHECK(o.ask().offered);
    o.memory.available = 0; o.memory.swap_free = 450;
    CHECK(o.ask().offered);
}

TEST_CASE("SPEC-145 with 449 MiB the row is absent and the text names the number [parts: download-all]") {
    Offer o;
    o.memory.available = 249;
    const spz::DownloadOffer offer = o.ask();
    CHECK_FALSE(offer.offered);
    CHECK(offer.text == "MSG_NEED_MEMORY");
    REQUIRE(offer.about.count("n") == 1);
    CHECK(offer.about.at("n") == "449");
    CHECK(offer.memory_mib == 449);
}

TEST_CASE("SPEC-145 each other condition, failed alone, gives its own text [parts: download-all]") {
    {
        Offer o;
        o.unpacker.is_available = false;
        const spz::DownloadOffer offer = o.ask();
        CHECK_FALSE(offer.offered);
        CHECK(offer.text == "MSG_NEED_7ZIP");
        CHECK_FALSE(offer.unpacker);
    }
    {
        Offer o;
        o.files.set_free_bytes(699 * kMiB);
        const spz::DownloadOffer offer = o.ask();
        CHECK_FALSE(offer.offered);
        CHECK(offer.text == "MSG_NEED_ROOM");
        REQUIRE(offer.about.count("n") == 1);
        REQUIRE(offer.about.count("m") == 1);
        CHECK(offer.about.at("n") == "700");
        CHECK(offer.about.at("m") == "699");
        CHECK(offer.free_mib == 699);
    }
    {
        Offer o;
        o.files.make_read_only(o.temp.path.string());      // the folder that holds the collection root
        const spz::DownloadOffer offer = o.ask();
        CHECK_FALSE(offer.offered);
        CHECK(offer.text == "MSG_NEED_WRITE");
        CHECK_FALSE(offer.can_write);
    }
}

TEST_CASE("SPEC-145 with HVSC not set up, or its release not known, the row is simply absent: no text [parts: download-all]") {
    Offer not_set_up;
    not_set_up.status.set_up = false;
    not_set_up.status.release = 0;
    spz::DownloadOffer offer = not_set_up.ask();
    CHECK_FALSE(offer.offered);
    CHECK(offer.text == "");

    Offer no_release;
    no_release.status.release = 0;
    offer = no_release.ask();
    CHECK_FALSE(offer.offered);
    CHECK(offer.text == "");
}

TEST_CASE("SPEC-145 SPEC-144 the conditions are checked again when the row is chosen: with too little memory by then, nothing is downloaded [parts: download-all]") {
    Complete c;
    c.memory.available = 249;                          // 449 MiB with the swap
    c.memory.swap_free = 200;
    c.download->start();
    c.run();
    CHECK(c.http.calls == 0);
    CHECK(c.unpacker.log.empty());
    CHECK(snapshot(c.root) == c.before);
    CHECK_FALSE(c.jobs.running());
    CHECK(c.notices.count("MSG_DOWNLOAD_DONE") == 0);
    if (!c.play.log.empty()) CHECK(c.play.log.back() == "on");

    Complete no_7z;
    no_7z.unpacker.is_available = false;
    no_7z.download->start();
    no_7z.run();
    CHECK(no_7z.http.calls == 0);
    CHECK(snapshot(no_7z.root) == no_7z.before);
    CHECK_FALSE(no_7z.jobs.running());
}

// --- SPEC-149 ------------------------------------------------------------------------------------------

TEST_CASE("SPEC-149 an archive whose every entry is a file or folder under C64Music passes [parts: download-all]") {
    std::vector<ArchiveEntry> entries;
    ArchiveEntry top;
    top.path = "C64Music";
    top.is_folder = true;
    entries.push_back(top);
    ArchiveEntry folder;
    folder.path = "C64Music/MUSICIANS";
    folder.is_folder = true;
    entries.push_back(folder);
    entries.push_back(file_entry("C64Music/MUSICIANS/a.sid"));
    entries.push_back(file_entry("C64Music/DOCUMENTS/Songlengths.md5", 5000000));
    entries.push_back(file_entry("C64Music/a name with spaces.txt"));
    entries.push_back(file_entry("C64Music/..hidden"));            // two dots in a name are not a ".." part
    CHECK(spz::check_archive(entries) == ArchiveFault::None);
    CHECK(spz::check_archive(entries_of(spz::test::fixture_path("collections/85"))) == ArchiveFault::None);
}

TEST_CASE("SPEC-149 an archive is refused for any one entry that could land outside the collection, or that is a link or a special file [parts: download-all]") {
    struct Row {
        const char* path;
        ArchiveFault fault;        // why it is refused
        ArchiveFault or_fault;     // or this, where a path breaks two rules at once
    };
    const ArchiveFault outside = ArchiveFault::OutsideTopFolder;
    const std::vector<Row> rows = {
        {"/etc/passwd", ArchiveFault::AbsolutePath, outside},
        {"/C64Music/MUSICIANS/a.sid", ArchiveFault::AbsolutePath, outside},
        {"C64Music//a.sid", ArchiveFault::EmptyPart, ArchiveFault::EmptyPart},
        {"C64Music/MUSICIANS/", ArchiveFault::EmptyPart, ArchiveFault::EmptyPart},
        {"C64Music/./a.sid", ArchiveFault::DotPart, ArchiveFault::DotPart},
        {"C64Music/../outside.sid", ArchiveFault::DotPart, ArchiveFault::DotPart},
        {"C64Music/MUSICIANS/..", ArchiveFault::DotPart, ArchiveFault::DotPart},
        {"../outside.sid", ArchiveFault::DotPart, outside},
        {"C64Music\\a.sid", ArchiveFault::Backslash, outside},
        {"C64Music/MUSICIANS\\..\\..\\a.sid", ArchiveFault::Backslash, ArchiveFault::Backslash},
        {"Other/a.sid", outside, outside},
        {"C64Music2/a.sid", outside, outside},
        {"c64music/a.sid", outside, outside},
        {"readme.txt", outside, outside},
        {"", outside, ArchiveFault::EmptyPart},
    };
    for (const Row& row : rows) {
        CAPTURE(row.path);
        std::vector<ArchiveEntry> entries = {file_entry("C64Music/MUSICIANS/a.sid"), file_entry(row.path),
                                             file_entry("C64Music/MUSICIANS/b.sid")};
        const ArchiveFault fault = spz::check_archive(entries);
        CHECK(fault != ArchiveFault::None);
        CHECK((fault == row.fault || fault == row.or_fault));
    }

    ArchiveEntry link = file_entry("C64Music/MUSICIANS/link.sid");
    link.is_link = true;
    CHECK(spz::check_archive({file_entry("C64Music/a.sid"), link}) == ArchiveFault::Link);
    ArchiveEntry special = file_entry("C64Music/MUSICIANS/pipe");
    special.is_special = true;
    CHECK(spz::check_archive({special}) == ArchiveFault::Special);
}

TEST_CASE("SPEC-149 an archive is refused with more than 200,000 entries, or with sizes that add up to more than 2 GiB [parts: download-all]") {
    std::vector<ArchiveEntry> many;
    many.reserve(200001);
    for (int i = 0; i < 200000; ++i) many.push_back(file_entry("C64Music/T" + std::to_string(i) + ".sid", 1));
    CHECK(spz::check_archive(many) == ArchiveFault::None);
    many.push_back(file_entry("C64Music/One_More.sid", 1));
    CHECK(spz::check_archive(many) == ArchiveFault::TooManyEntries);

    const std::uint64_t gib = 1024 * kMiB;
    std::vector<ArchiveEntry> large = {file_entry("C64Music/a.sid", gib), file_entry("C64Music/b.sid", gib)};
    CHECK(spz::check_archive(large) == ArchiveFault::None);                // 2 GiB exactly
    large.push_back(file_entry("C64Music/c.sid", 1));
    CHECK(spz::check_archive(large) == ArchiveFault::TooLarge);
    // Sizes no 64-bit sum can hold do not get past the check either.
    std::vector<ArchiveEntry> huge = {file_entry("C64Music/a.sid", ~0ull), file_entry("C64Music/b.sid", ~0ull)};
    CHECK(spz::check_archive(huge) == ArchiveFault::TooLarge);
}

TEST_CASE("SPEC-149 the reader of 7z's listing gives each entry's path, size and kind [parts: download-all]") {
    // What `7z l -slt` of 7-Zip 23.01 prints for a small archive, with a link and a pipe added by hand.
    const std::string text =
        "\n7-Zip 23.01 (x64) : Copyright (c) 1999-2023 Igor Pavlov : 2023-06-20\n"
        " 64-bit locale=C.UTF-8 Threads:4 OPEN_MAX:1024\n\n"
        "Scanning the drive for archives:\n1 file, 247 bytes (1 KiB)\n\n"
        "Listing archive: /tmp/x/good.7z\n\n--\n"
        "Path = /tmp/x/good.7z\nType = 7z\nPhysical Size = 247\nHeaders Size = 234\nMethod = LZMA2:12\nSolid = +\nBlocks = 1\n\n"
        "----------\n"
        "Path = C64Music\nSize = 0\nPacked Size = 0\nModified = 2026-11-25 21:19:44.7512123\nAttributes = D drwxr-xr-x\nCRC = \nEncrypted = -\nMethod = \nBlock = \n\n"
        "Path = C64Music/DOCUMENTS\nSize = 0\nPacked Size = 0\nModified = 2026-11-25 21:19:44\nAttributes = D_ drwxr-xr-x\nCRC = \nEncrypted = -\nMethod = \nBlock = \n\n"
        "Path = C64Music/DOCUMENTS/Songlengths.md5\nSize = 5131488\nPacked Size = 13\nModified = 2026-11-25 21:19:44\nAttributes = A -rw-r--r--\nCRC = 788EE22F\nEncrypted = -\nMethod = LZMA2:12\nBlock = 0\n\n"
        "Path = C64Music/MUSICIANS/A/a name = with an equals sign.sid\nSize = 5\nPacked Size = \nModified = 2026-11-25 21:19:44\nAttributes = A_ -rw-r--r--\nCRC = 2C87716A\nEncrypted = -\nMethod = LZMA2:12\nBlock = 0\n\n"
        "Path = C64Music/out.sid\nSize = 13\nPacked Size = \nModified = 2026-11-25 21:19:44\nAttributes = A lrwxrwxrwx\nCRC = 1B2C3D4E\nEncrypted = -\nMethod = LZMA2:12\nBlock = 0\n\n"
        "Path = C64Music/pipe\nSize = 0\nPacked Size = 0\nModified = 2026-11-25 21:19:44\nAttributes = A prw-r--r--\nCRC = \nEncrypted = -\nMethod = \nBlock = \n\n"
        "Path = ../evil.sid\nSize = 2\nPacked Size = \nModified = 2026-11-25 21:19:44\nAttributes = A -rw-r--r--\nCRC = 1B2C3D4E\nEncrypted = -\nMethod = LZMA2:12\nBlock = 0\n\n";

    std::vector<ArchiveEntry> entries;
    REQUIRE(spz::read_7z_listing(text, &entries));
    REQUIRE(entries.size() == 7);

    CHECK(entries[0].path == "C64Music");
    CHECK(entries[0].is_folder);
    CHECK(entries[0].size == 0);
    CHECK(entries[1].path == "C64Music/DOCUMENTS");
    CHECK(entries[1].is_folder);
    CHECK(entries[2].path == "C64Music/DOCUMENTS/Songlengths.md5");
    CHECK(entries[2].size == 5131488);
    CHECK_FALSE(entries[2].is_folder);
    CHECK_FALSE(entries[2].is_link);
    CHECK_FALSE(entries[2].is_special);
    CHECK(entries[3].path == "C64Music/MUSICIANS/A/a name = with an equals sign.sid");
    CHECK(entries[3].size == 5);
    CHECK(entries[4].path == "C64Music/out.sid");
    CHECK(entries[4].is_link);
    CHECK(entries[5].path == "C64Music/pipe");
    CHECK(entries[5].is_special);
    CHECK(entries[6].path == "../evil.sid");       // the path as the archive has it: the check refuses it

    CHECK(spz::check_archive(entries) != ArchiveFault::None);
    entries.resize(4);
    CHECK(spz::check_archive(entries) == ArchiveFault::None);

    // With CRLF line endings it reads the same.
    std::string crlf;
    for (const char c : text) crlf += c == '\n' ? std::string("\r\n") : std::string(1, c);
    std::vector<ArchiveEntry> again;
    REQUIRE(spz::read_7z_listing(crlf, &again));
    REQUIRE(again.size() == 7);
    CHECK(again[3].path == "C64Music/MUSICIANS/A/a name = with an equals sign.sid");
    CHECK(again[4].is_link);
}

TEST_CASE("SPEC-149 text that is not a listing of 7z is not read as one [parts: download-all]") {
    std::vector<ArchiveEntry> entries;
    CHECK_FALSE(spz::read_7z_listing("", &entries));
    CHECK_FALSE(spz::read_7z_listing("ERROR: archive.7z\nCan not open the file as archive\n", &entries));
    CHECK_FALSE(spz::read_7z_listing(std::string(100000, 'x'), &entries));
    // A size that is not a number makes the whole listing unreadable, rather than an entry of size 0.
    CHECK_FALSE(spz::read_7z_listing("Listing archive: a.7z\n\n--\nPath = a.7z\nType = 7z\n\n----------\n"
                                     "Path = C64Music/a.sid\nSize = lots\nAttributes = A -rw-r--r--\n\n", &entries));
}

#endif  // SPZ_PART_DOWNLOAD_ALL
