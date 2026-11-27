// SPDX-License-Identifier: GPL-3.0-or-later
// Setting HVSC up (SPEC-140), with the room it needs on the card (SPEC-146), its progress and stopping
// (SPEC-147), and the reader of hv_sids.txt (SPEC-148), which setting up needs whether or not the part
// `releases` is built.
#include <algorithm>
#include <string>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "hvsc_support.h"
#include "spz/fetcher.h"
#include "spz/hvsc_setup.h"
#include "spz/listing.h"
#include "support.h"

using namespace spz::test::hvsc;
using spz::test::bytes;
using spz::test::fixture;

namespace {

// A world in which the notice has been accepted, with "Get HVSC" ready to be chosen.
struct Setup : World {
    HvscEvents events;
    spz::HvscSetup setup{setup_parts(*this, &events)};

    Setup() { record.accepted = true; }

    std::vector<std::string> document_urls() const {
        std::vector<std::string> list;
        for (const std::string& document : kDocuments) list.push_back(kUrlFiles + "/" + document);
        std::sort(list.begin(), list.end());
        return list;
    }
    std::vector<std::string> requested() const {
        std::vector<std::string> list = urls(http);
        std::sort(list.begin(), list.end());
        return list;
    }
};

spz::fake::HttpClient::Answer body_of(const std::string& text) {
    spz::fake::HttpClient::Answer answer;
    answer.body = bytes(text);
    return answer;
}

}  // namespace

TEST_CASE("SPEC-140 with the fixture documents served, Get HVSC ends with HVSC set up, and the request log is exactly the five documents") {
    Setup s;
    serve_collection(&s.http, 85);

    REQUIRE(s.setup.start());
    CHECK(s.jobs.running());                         // it is a job from the moment it is chosen
    CHECK(s.jobs.state().kind == spz::JobKind::Setup);
    CHECK(s.http.calls == 0);                        // and it runs on the long worker, not here
    CHECK(s.long_worker.waiting() >= 1);
    s.run();

    // Exactly the five documents, each whole, each with a document's limit, each into the cache.
    CHECK(s.requested() == s.document_urls());
    for (const spz::HttpRequest& request : s.http.log) {
        CAPTURE(request.url);
        CHECK(request.limit_bytes == 32 * kMiB);
        CHECK(request.resume_from == 0);
        CHECK(starts_with(request.to_path, s.cache + "/"));
    }

    // They are in DOCUMENTS under the root, byte for byte, and nothing else is in the root.
    std::map<std::string, spz::Bytes> expected;
    for (const std::string& document : kDocuments) expected[document] = fixture(collection(85) + "/" + document);
    CHECK(snapshot(s.root) == expected);

    // Step 6: the listing is built, the notice says so, and the app is told to open it and start the index.
    CHECK(s.notices.ids() == std::vector<std::string>{"MSG_HVSC_READY"});
    CHECK(s.notices.about("MSG_HVSC_READY", "n") == "85");
    CHECK(s.events.events == std::vector<std::string>{"ready 85"});
    CHECK_FALSE(s.jobs.running());

    spz::Listing listing;
    REQUIRE(listing.open(&s.files, s.listing_file(), s.database()));
    CHECK(listing.tune_count() == 11);
    std::vector<std::string> folders, tunes;
    REQUIRE(listing.folder("/", &folders, &tunes));
    CHECK(folders == std::vector<std::string>{"DEMOS", "GAMES", "MUSICIANS"});

    const spz::HvscStatus status = spz::hvsc_status(&s.files, listing, s.root);
    CHECK(status.set_up);
    CHECK(status.release == 85);

    // What was downloaded has been moved: the cache holds the listing and no other file.
    std::vector<std::string> left;
    for (const auto& file : snapshot(s.cache)) left.push_back(file.first);
    CHECK(left == std::vector<std::string>{"listing.bin"});
}

TEST_CASE("SPEC-140 SPEC-141 after the set-up, an absent tune that is chosen is fetched and is there to be played") {
    Setup s;
    serve_collection(&s.http, 85);
    REQUIRE(s.setup.start());
    s.run();
    spz::Listing listing;
    REQUIRE(listing.open(&s.files, s.listing_file(), s.database()));

    Heard heard;
    spz::Fetcher fetcher(fetcher_parts(s, &listing));
    fetcher.set_listener(&heard);
    CHECK_FALSE(exists(s.in_root(kCommando)));
    CHECK(fetcher.request(kCommando, spz::FetchWhy::Chosen) == spz::FetchAnswer::Fetching);
    s.run();

    REQUIRE(s.http.log.size() == 6);
    CHECK(s.http.log[5].url == kUrlFiles + kCommando);
    CHECK(read_file(s.in_root(kCommando)) == fixture("tunes/commando_test.sid"));
    CHECK(heard.events == std::vector<std::string>{"fetched " + kCommando});
}

TEST_CASE("SPEC-140 SPEC-132 each document is moved into DOCUMENTS through a temporary file beside it, the song length file last") {
    Setup s;
    serve_collection(&s.http, 85);
    REQUIRE(s.setup.start());
    s.run();
    CHECK(s.files.calls > 0);

    // The renames that put a file in the root, in the order they were made.
    std::vector<std::string> placed;
    for (std::size_t i = 0; i < s.files.log.size(); ++i) {
        const auto& op = s.files.log[i];
        if (op.what != "rename" || !starts_with(op.to, s.root + "/")) continue;
        CAPTURE(op.to);
        placed.push_back(op.to.substr(s.root.size() + 1));
        CHECK(op.path == temporary_of(op.to));                       // from its temporary name, in the same folder
        bool synced_before = false, folder_synced_after = false;
        for (std::size_t j = 0; j < i; ++j) {
            synced_before = synced_before || (s.files.log[j].what == "sync" && s.files.log[j].path == op.path);
        }
        for (std::size_t j = i + 1; j < s.files.log.size(); ++j) {
            folder_synced_after = folder_synced_after ||
                                  (s.files.log[j].what == "sync" && s.files.log[j].path == s.root + "/DOCUMENTS");
        }
        CHECK(synced_before);
        CHECK(folder_synced_after);
    }
    REQUIRE(placed.size() == 5);
    CHECK(placed.back() == "DOCUMENTS/Songlengths.md5");
    std::sort(placed.begin(), placed.end());
    std::vector<std::string> documents = kDocuments;
    std::sort(documents.begin(), documents.end());
    CHECK(placed == documents);

    // No temporary file is left, and nothing was written outside the root and the cache.
    for (const auto& file : snapshot(s.root)) CHECK(file.first.find(".tmp-sidplayzero") == std::string::npos);
    for (const auto& op : changes(s.files)) {
        CAPTURE(op.what);
        CAPTURE(op.path);
        CHECK((starts_with(op.path, s.root) || starts_with(op.path, s.cache)));
    }
}

TEST_CASE("SPEC-140 with a download failing, the root is unchanged and the notice names step 3; the row can then be chosen again") {
    struct Fault {
        const char* what;
        std::function<void(spz::fake::HttpClient*)> make;
    };
    const std::vector<Fault> faults = {
        {"one document is not there", [](spz::fake::HttpClient* http) {
             spz::fake::HttpClient::Answer gone;
             gone.status = 404;
             http->answer(kUrlFiles + "/DOCUMENTS/hv_sids.txt", gone);
         }},
        {"the song length file is cut off half-way", [](spz::fake::HttpClient* http) {
             spz::fake::HttpClient::Answer cut;
             cut.body = fixture(collection(85) + "/DOCUMENTS/Songlengths.md5");
             cut.cut_after = cut.body.size() / 2;
             http->answer(kUrlFiles + "/DOCUMENTS/Songlengths.md5", cut);
         }},
        {"the server fails", [](spz::fake::HttpClient* http) {
             spz::fake::HttpClient::Answer error;
             error.status = 503;
             http->answer(kUrlFiles + "/DOCUMENTS/STIL.txt", error);
         }},
        {"there is no network", [](spz::fake::HttpClient* http) { http->down = true; }},
    };
    for (const Fault& fault : faults) {
        CAPTURE(fault.what);
        Setup s;
        // The root holds a tune of the user's own, and no documents.
        write_file(s.root + "/My Rips/Cool S\xC3\xB6ng.sid", fixture("tunes/own_tune.sid"));
        const auto before = snapshot(s.root);
        serve_collection(&s.http, 85);
        fault.make(&s.http);

        REQUIRE(s.setup.start());
        s.run();
        CHECK(snapshot(s.root) == before);
        CHECK(s.notices.ids() == std::vector<std::string>{"MSG_HVSC_FAILED"});
        CHECK(s.notices.about("MSG_HVSC_FAILED", "step") == "3");
        CHECK(s.events.events == std::vector<std::string>{"failed 3"});
        CHECK_FALSE(s.jobs.running());
        CHECK_FALSE(exists(s.listing_file()));

        // Chosen again, with the server in order: it works.
        s.http.down = false;
        serve_collection(&s.http, 85);
        REQUIRE(s.setup.start());
        s.run();
        CHECK(s.notices.count("MSG_HVSC_READY") == 1);
        CHECK(exists(s.database()));
        CHECK(read_file(s.root + "/My Rips/Cool S\xC3\xB6ng.sid") == fixture("tunes/own_tune.sid"));
    }
}

TEST_CASE("SPEC-140 a root that does not exist is still not there after a failed set-up") {
    Setup s;
    s.http.down = true;
    REQUIRE(s.setup.start());
    s.run();
    CHECK(snapshot(s.root).empty());
    CHECK_FALSE(exists(s.database()));
    CHECK(s.notices.about("MSG_HVSC_FAILED", "step") == "3");
}

TEST_CASE("SPEC-140 documents that do not pass the check fail step 4, and nothing is moved into the root") {
    struct Fault {
        const char* what;
        const char* document;
        const char* text;
    };
    const std::vector<Fault> faults = {
        {"a song length file with no tune in it", "DOCUMENTS/Songlengths.md5", "[Database]\r\n"},
        {"a song length file that is something else", "DOCUMENTS/Songlengths.md5", "<html><body>Not found</body></html>\n"},
        {"an hv_sids.txt that names no release", "DOCUMENTS/hv_sids.txt", "# stub for old updaters\r\nnothing here\r\n"},
        {"an empty hv_sids.txt", "DOCUMENTS/hv_sids.txt", ""},
    };
    for (const Fault& fault : faults) {
        CAPTURE(fault.what);
        Setup s;
        serve_collection(&s.http, 85);
        s.http.answer(kUrlFiles + "/" + fault.document, body_of(fault.text));

        REQUIRE(s.setup.start());
        s.run();
        CHECK(s.requested() == s.document_urls());
        CHECK(snapshot(s.root).empty());
        CHECK(s.notices.count("MSG_HVSC_FAILED") == 1);
        CHECK(s.notices.about("MSG_HVSC_FAILED", "step") == "4");
        CHECK(s.notices.count("MSG_HVSC_READY") == 0);
        CHECK(s.events.events == std::vector<std::string>{"failed 4"});
        CHECK_FALSE(s.jobs.running());
    }
}

TEST_CASE("SPEC-140 a root that cannot be written fails step 5, and HVSC is not set up") {
    Setup s;
    serve_collection(&s.http, 85);
    s.files.make_read_only(s.root);

    REQUIRE(s.setup.start());
    s.run();
    CHECK(snapshot(s.root).empty());
    CHECK(s.notices.count("MSG_HVSC_FAILED") == 1);
    CHECK(s.notices.about("MSG_HVSC_FAILED", "step") == "5");
    CHECK(s.notices.count("MSG_HVSC_READY") == 0);
    CHECK_FALSE(s.jobs.running());
}

TEST_CASE("SPEC-140 a move that fails half-way leaves no song length file in the root, so HVSC is not half set up") {
    Setup s;
    serve_collection(&s.http, 85);
    // The fourth rename into the root fails: three documents are in place, and the song length file,
    // which is moved last, is not.
    int renames = 0;
    s.files.before = [&](const spz::fake::Files::Operation& op) {
        if (op.what == "rename" && starts_with(op.to, s.root + "/") && ++renames == 4) s.files.fail_at(1);
    };

    REQUIRE(s.setup.start());
    s.run();
    CHECK_FALSE(exists(s.database()));
    CHECK(s.notices.about("MSG_HVSC_FAILED", "step") == "5");
    CHECK(s.notices.count("MSG_HVSC_READY") == 0);
    CHECK_FALSE(exists(s.listing_file()));
}

TEST_CASE("SPEC-146 with one MiB less than the 16 MiB a set-up needs, no request is made and the notice gives both figures; with exactly enough, the download starts") {
    {
        Setup s;
        serve_collection(&s.http, 85);
        s.files.set_free_bytes(15 * kMiB);
        REQUIRE(s.setup.start());
        s.run();
        CHECK(s.http.calls == 0);
        CHECK(s.notices.count("MSG_NEED_ROOM") == 1);
        CHECK(s.notices.about("MSG_NEED_ROOM", "n") == "16");
        CHECK(s.notices.about("MSG_NEED_ROOM", "m") == "15");
        CHECK(s.notices.count("MSG_HVSC_READY") == 0);
        CHECK(snapshot(s.root).empty());
        CHECK_FALSE(s.jobs.running());
        CHECK(s.files.count("free_bytes") >= 1);     // the figure came from the file system
    }
    {
        Setup s;
        serve_collection(&s.http, 85);
        s.files.set_free_bytes(16 * kMiB);
        REQUIRE(s.setup.start());
        s.run();
        CHECK(s.http.log.size() == 5);
        CHECK(s.notices.count("MSG_NEED_ROOM") == 0);
        CHECK(s.notices.count("MSG_HVSC_READY") == 1);
    }
}

TEST_CASE("SPEC-146 what is about to be written is compared with the free space, and both are told in MiB") {
    spz::test::TempFolder temp;
    spz::fake::Files files;

    files.set_free_bytes(15 * kMiB);
    spz::Room room = spz::check_room(&files, temp.file("C64Music"), 16 * kMiB);
    CHECK_FALSE(room.enough);
    CHECK(room.needed_mib == 16);
    CHECK(room.free_mib == 15);

    files.set_free_bytes(16 * kMiB);
    room = spz::check_room(&files, temp.file("C64Music"), 16 * kMiB);
    CHECK(room.enough);

    files.set_free_bytes(16 * kMiB - 1);              // a single byte short is short
    room = spz::check_room(&files, temp.file("C64Music"), 16 * kMiB);
    CHECK_FALSE(room.enough);

    files.set_free_bytes(699 * kMiB);
    room = spz::check_room(&files, temp.file("C64Music"), 700 * kMiB);
    CHECK_FALSE(room.enough);
    CHECK(room.needed_mib == 700);
    CHECK(room.free_mib == 699);

    // The question is asked about the destination, which need not exist yet.
    bool asked = false;
    for (const auto& op : files.log) asked = asked || (op.what == "free_bytes" && op.path == temp.file("C64Music"));
    CHECK(asked);
}

TEST_CASE("SPEC-147 SPEC-140 while a document's download is held open the job is in step 3, and no other job can begin") {
    Setup s;
    serve_collection(&s.http, 85);
    for (const std::string& document : kDocuments) {
        spz::fake::HttpClient::Answer answer;
        answer.body = fixture(collection(85) + "/" + document);
        answer.stall = true;                          // whichever document is asked for first is held
        s.http.answer(kUrlFiles + "/" + document, answer);
    }
    std::vector<std::string> held_urls;
    for (const std::string& document : kDocuments) held_urls.push_back(kUrlFiles + "/" + document);

    REQUIRE(s.setup.start());
    {
        HeldWorker worker(&s.http, held_urls);
        worker.start(&s.long_worker);
        REQUIRE(worker.wait_for_stalled());

        const spz::JobState state = s.jobs.state();
        CHECK(state.kind == spz::JobKind::Setup);
        CHECK(state.step == 3);
        CHECK(state.can_stop);
        CHECK(spz::job_percent(state) < 100);

        CHECK_FALSE(s.setup.start());                 // one job at a time (SPEC-140)
        CHECK(s.http.log.size() == 1);

        worker.release_and_join();
    }
    s.run();
    CHECK(s.http.log.size() == 5);
    CHECK(s.notices.count("MSG_HVSC_READY") == 1);
    CHECK_FALSE(s.jobs.running());
    CHECK(s.jobs.state().kind == spz::JobKind::None);
}

TEST_CASE("SPEC-147 SPEC-140 stopping a set-up leaves the root as it was, nothing in the cache, and makes no further request") {
    Setup s;
    write_file(s.root + "/My Rips/Cool S\xC3\xB6ng.sid", fixture("tunes/own_tune.sid"));
    const auto before = snapshot(s.root);
    std::vector<std::string> held_urls;
    for (const std::string& document : kDocuments) {
        spz::fake::HttpClient::Answer answer;
        answer.body = fixture(collection(85) + "/" + document);
        answer.stall = true;
        s.http.answer(kUrlFiles + "/" + document, answer);
        held_urls.push_back(kUrlFiles + "/" + document);
    }

    REQUIRE(s.setup.start());
    {
        HeldWorker worker(&s.http, held_urls);
        worker.start(&s.long_worker);
        REQUIRE(worker.wait_for_stalled());
        CHECK(s.jobs.stop());                         // "Stop", confirmed
        CHECK(worker.wait_for_end());                 // the held request is cancelled, and the job ends itself
    }
    s.run();
    CHECK_FALSE(s.jobs.running());
    CHECK(s.http.log.size() == 1);
    CHECK(snapshot(s.root) == before);
    CHECK(snapshot(s.cache).empty());                 // a document is downloaded whole each time: no partial file
    CHECK(s.notices.count("MSG_HVSC_READY") == 0);
    CHECK(s.events.count("ready") == 0);

    // The row can be chosen again.
    serve_collection(&s.http, 85);
    REQUIRE(s.setup.start());
    s.run();
    CHECK(s.notices.count("MSG_HVSC_READY") == 1);
}

TEST_CASE("SPEC-148 SPEC-140 the release is the first number after the word Release in hv_sids.txt") {
    CHECK(spz::read_release(fixture(collection(85) + "/DOCUMENTS/hv_sids.txt")) == 85);
    CHECK(spz::read_release(fixture(collection(84) + "/DOCUMENTS/hv_sids.txt")) == 84);
    CHECK(spz::read_release(bytes("# stub for old updaters\r\nRelease 85\r\n")) == 85);
    CHECK(spz::read_release(bytes("# stub for old updaters\nRelease 85\n")) == 85);
    CHECK(spz::read_release(bytes("Release 9")) == 9);
    CHECK(spz::read_release(bytes("                Release 100\r\n")) == 100);
    CHECK(spz::read_release(bytes("Release 85\r\nRelease 86\r\n")) == 85);
    CHECK(spz::read_release(bytes("12 files\r\nRelease 85 of 99\r\n")) == 85);
    CHECK(spz::read_release(bytes("Release: #85\r\n")) == 85);

    // No release: 0.
    CHECK(spz::read_release(bytes("")) == 0);
    CHECK(spz::read_release(bytes("# stub for old updaters\r\n")) == 0);
    CHECK(spz::read_release(bytes("Release\r\n")) == 0);
    CHECK(spz::read_release(bytes("85\r\n")) == 0);
    CHECK(spz::read_release(bytes("85 is the Release\r\n")) == 0);
    CHECK(spz::read_release(spz::Bytes(4096, 0xFF)) == 0);
}

TEST_CASE("SPEC-140 HVSC is set up when the root's song length file gave a listing, and its release is known when hv_sids.txt names one") {
    World w;
    spz::Listing none;
    spz::HvscStatus status = spz::hvsc_status(&w.files, none, w.root);
    CHECK_FALSE(status.set_up);                       // no root at all
    CHECK(status.release == 0);

    w.put_documents(85);
    status = spz::hvsc_status(&w.files, none, w.root);
    CHECK_FALSE(status.set_up);                       // documents, but no listing has been made of them

    spz::Listing listing;
    REQUIRE(w.open_listing(&listing));
    status = spz::hvsc_status(&w.files, listing, w.root);
    CHECK(status.set_up);
    CHECK(status.release == 85);

    // "HVSC, release not known": the song lengths are there and hv_sids.txt is not, or names nothing.
    fs::remove(w.root + "/DOCUMENTS/hv_sids.txt");
    status = spz::hvsc_status(&w.files, listing, w.root);
    CHECK(status.set_up);
    CHECK(status.release == 0);
    write_file(w.root + "/DOCUMENTS/hv_sids.txt", bytes("# stub for old updaters\r\n"));
    status = spz::hvsc_status(&w.files, listing, w.root);
    CHECK(status.set_up);
    CHECK(status.release == 0);
}

TEST_CASE("SPEC-140 SPEC-131 a set-up removes the mark of a complete collection, since it brings the documents only (FIND-031)") {
    World w;
    w.record.accepted = true;
    spz::fake::Files state_files;
    spz::fake::Executor quick;
    spz::StateFile state{&state_files, &w.clock, &quick, w.temp.file("state/state.ini")};
    state.set_hvsc_complete_release(85);
    HvscEvents events;
    spz::HvscSetupParts parts = setup_parts(w, &events);
    parts.state = &state;
    spz::HvscSetup setup{parts};
    serve_collection(&w.http, 85);
    REQUIRE(setup.start());
    w.run();
    CHECK(events.events == std::vector<std::string>{"ready 85"});
    CHECK(state.state().hvsc_complete_release == 0);
}

TEST_CASE("SPEC-140 SPEC-131 a set-up that fails leaves the mark of a complete collection as it was (FIND-031)") {
    World w;
    w.record.accepted = true;
    spz::fake::Files state_files;
    spz::fake::Executor quick;
    spz::StateFile state{&state_files, &w.clock, &quick, w.temp.file("state/state.ini")};
    state.set_hvsc_complete_release(85);
    HvscEvents events;
    spz::HvscSetupParts parts = setup_parts(w, &events);
    parts.state = &state;
    spz::HvscSetup setup{parts};
    // Nothing is served: the documents' download fails, and the root is left as it was.
    REQUIRE(setup.start());
    w.run();
    CHECK(events.events.size() == 1);
    CHECK(events.count("failed") == 1);
    CHECK(state.state().hvsc_complete_release == 85);
}
