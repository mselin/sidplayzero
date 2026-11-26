// SPDX-License-Identifier: GPL-3.0-or-later
// Fetching a tune when it is played (SPEC-141): the fetcher itself, as the player drives it. One fetch at
// a time, the 700 ms of a step, the fetch-ahead, six unattended requests a minute, the room on the card,
// the checks on what arrives and where it lands. Then SPEC-143's 60 s after a failure, and the fetcher's
// side of SPEC-154: a tune is only ever written at an HVSC path the listing names.
//
// What the player does with the answers (its states, the 2 s it shows a failure for, the next entry) is
// tested with the player.
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "hvsc_support.h"
#include "spz/fetcher.h"
#include "spz/md5.h"
#include "support.h"

using namespace spz::test::hvsc;
using spz::FetchAnswer;
using spz::FetchWhy;
using spz::test::bytes;
using spz::test::fixture;

namespace {

// HVSC set up with the fixture collection's documents, every tune absent, the fake server offering them
// all, and the notice accepted.
struct Fetch : World {
    spz::Listing listing;
    Heard heard;
    Arrivals arrivals;
    std::unique_ptr<spz::Fetcher> fetcher;

    explicit Fetch(const std::string& database_text = "", spz::ChangedTunes* changed = nullptr) {
        record.accepted = true;
        put_documents(85);
        if (!database_text.empty()) write_file(database(), bytes(database_text));
        serve_collection(&http, 85);
        if (!open_listing(&listing)) throw std::runtime_error("the fixture database gave no listing");
        spz::FetcherParts parts = fetcher_parts(*this, &listing, &arrivals);
        parts.changed = changed;
        fetcher.reset(new spz::Fetcher(parts));
        fetcher->set_listener(&heard);
    }

    static std::string url(const std::string& path) { return kUrlFiles + path; }
    FetchAnswer request(const std::string& path, FetchWhy why) { return fetcher->request(path, why); }

    // The fake server holds the answer for this tune back until it is released.
    void hold(const std::string& path) {
        spz::fake::HttpClient::Answer answer;
        answer.body = fixture(collection(85) + path);
        answer.stall = true;
        http.answer(url(path), answer);
    }
    void answer_with(const std::string& path, const spz::Bytes& body) {
        spz::fake::HttpClient::Answer answer;
        answer.body = body;
        http.answer(url(path), answer);
    }
    void fail_with(const std::string& path, spz::HttpFailure failure) {
        spz::fake::HttpClient::Answer answer;
        answer.failure = failure;
        http.answer(url(path), answer);
    }
    void status(const std::string& path, int code) {
        spz::fake::HttpClient::Answer answer;
        answer.status = code;
        http.answer(url(path), answer);
    }
};

using Events = std::vector<std::string>;

}  // namespace

TEST_CASE("SPEC-141 playing an absent tune with Enter makes one request at once, for exactly that path, and the file appears at its HVSC path") {
    Fetch f;
    CHECK_FALSE(exists(f.in_root(kCommando)));

    REQUIRE(f.request(kCommando, FetchWhy::Chosen) == FetchAnswer::Fetching);
    CHECK(f.fetcher->fetching(kCommando));
    CHECK(f.http.calls == 0);                     // the request is the fetch worker's to make (SPEC-022)
    CHECK(f.fetch_worker.waiting() >= 1);         // and it is there now: nothing waits for the clock
    CHECK(f.clock.timers_waiting() == 0);
    f.run();

    REQUIRE(f.http.log.size() == 1);
    const spz::HttpRequest& request = f.http.log[0];
    CHECK(request.url == "https://hvsc.c64.org/download/C64Music/MUSICIANS/H/Hubbard_Rob/Commando.sid");
    CHECK(request.limit_bytes == 1 * kMiB);       // SPEC-150: 1 MiB for a tune
    CHECK(request.resume_from == 0);
    CHECK(request.to_path == temporary_of(f.in_root(kCommando)));   // a temporary file in the tune's own folder

    CHECK(read_file(f.in_root(kCommando)) == fixture("tunes/commando_test.sid"));
    CHECK_FALSE(exists(temporary_of(f.in_root(kCommando))));
    CHECK(f.heard.events == Events{"fetched " + kCommando});        // the tune is present: it can be played
    CHECK(f.arrivals.paths == Events{kCommando});                   // and it is added to the index
    CHECK_FALSE(f.fetcher->fetching(kCommando));
}

TEST_CASE("SPEC-141 SPEC-132 SPEC-154 the fetched file is written under a temporary name in its folder, which is made first, then flushed and renamed; nothing is written anywhere else") {
    Fetch f;
    const auto documents = snapshot(f.root);
    f.files.log.clear();                          // what was done to build the listing is not this test's
    REQUIRE(f.request(kCommando, FetchWhy::Chosen) == FetchAnswer::Fetching);
    f.run();
    CHECK(f.files.calls > 0);

    const std::string target = f.in_root(kCommando), temporary = temporary_of(target);
    const std::string folder = f.root + "/MUSICIANS/H/Hubbard_Rob";
    int made = -1, synced = -1, renamed = -1, folder_synced = -1;
    for (std::size_t i = 0; i < f.files.log.size(); ++i) {
        const auto& op = f.files.log[i];
        const int at = static_cast<int>(i);
        if (op.what == "make_folders" && op.path == folder && made < 0) made = at;
        if (op.what == "sync" && op.path == temporary) synced = at;
        if (op.what == "rename" && op.path == temporary && op.to == target) renamed = at;
        if (op.what == "sync" && op.path == folder) folder_synced = at;
    }
    CHECK(made >= 0);
    CHECK(synced > made);
    CHECK(renamed > synced);
    CHECK(folder_synced > renamed);

    // Every change is under the root, and the one file that arrives is the tune at its listed path.
    for (const auto& op : changes(f.files)) {
        CAPTURE(op.what);
        CAPTURE(op.path);
        CHECK(starts_with(op.path, f.root + "/"));
        if (op.what == "rename") CHECK(op.to == target);
    }
    auto expected = documents;
    expected[kCommando.substr(1)] = fixture("tunes/commando_test.sid");
    CHECK(snapshot(f.root) == expected);
}

TEST_CASE("SPEC-141 playing a tune again makes no request") {
    Fetch f;
    REQUIRE(f.request(kCommando, FetchWhy::Chosen) == FetchAnswer::Fetching);
    f.run();
    REQUIRE(f.http.log.size() == 1);

    CHECK(f.request(kCommando, FetchWhy::Chosen) == FetchAnswer::Present);
    f.run();
    CHECK(f.http.log.size() == 1);

    // In a later run nothing remembers the fetch, and the file is simply there.
    Heard later_heard;
    spz::Fetcher later(fetcher_parts(f, &f.listing));
    later.set_listener(&later_heard);
    later.request(kCommando, FetchWhy::Chosen);
    f.run();
    CHECK(f.http.log.size() == 1);
    CHECK(later_heard.events == Events{"fetched " + kCommando});
}

TEST_CASE("SPEC-141 the request is url_files followed by the tune's HVSC path, as the setting is when it is made") {
    Fetch f;
    f.settings.url_files = "https://mirror.example.test/hvsc/C64Music";
    spz::fake::HttpClient::Answer answer;
    answer.body = fixture("tunes/three_subtunes.sid");
    f.http.answer("https://mirror.example.test/hvsc/C64Music" + kThreeSongs, answer);

    REQUIRE(f.request(kThreeSongs, FetchWhy::Chosen) == FetchAnswer::Fetching);
    f.run();
    CHECK(urls(f.http) == Events{"https://mirror.example.test/hvsc/C64Music/MUSICIANS/T/Tester_Tom/Three_Songs.sid"});
    CHECK(read_file(f.in_root(kThreeSongs)) == fixture("tunes/three_subtunes.sid"));
}

TEST_CASE("SPEC-141 an answer with other bytes fails with MSG_FETCH_MISMATCH and leaves no file") {
    Fetch f;
    const auto before = snapshot(f.root);
    f.answer_with(kCommando, fixture("tunes/two_sid.sid"));         // a tune, but not the one HVSC records there

    REQUIRE(f.request(kCommando, FetchWhy::Chosen) == FetchAnswer::Fetching);
    f.run();
    CHECK(f.http.log.size() == 1);
    CHECK(f.heard.events == Events{"failed " + kCommando + " MSG_FETCH_MISMATCH"});
    CHECK(snapshot(f.root) == before);
    CHECK_FALSE(exists(f.in_root(kCommando)));
    CHECK_FALSE(exists(temporary_of(f.in_root(kCommando))));
    CHECK(f.arrivals.paths.empty());
    CHECK_FALSE(f.fetcher->fetching(kCommando));
}

TEST_CASE("SPEC-141 SPEC-203 a fetched file that does not pass the tune check is not stored, even with the MD5 the listing gives") {
    // A database of the test's own that records, for one path, the MD5 of a file that is no tune.
    const spz::Bytes not_a_tune = fixture("tunes/bad_magic.sid");
    const std::string path = "/MUSICIANS/X/Bad_Magic.sid";
    Fetch f("[Database]\r\n; " + path + "\r\n" + spz::md5_hex(not_a_tune) + "=0:02\r\n");
    const auto before = snapshot(f.root);
    f.answer_with(path, not_a_tune);

    REQUIRE(f.request(path, FetchWhy::Chosen) == FetchAnswer::Fetching);
    f.run();
    CHECK(f.http.log.size() == 1);
    CHECK(f.heard.events == Events{"failed " + path + " MSG_INVALID_SID"});
    CHECK(snapshot(f.root) == before);
    CHECK(f.arrivals.paths.empty());
}

TEST_CASE("SPEC-141 an answer that is no tune at all and has another MD5 fails and leaves no file") {
    Fetch f;
    const auto before = snapshot(f.root);
    f.answer_with(kCommando, bytes("<html><body>Welcome to the hotel's network. Please log in.</body></html>\n"));

    REQUIRE(f.request(kCommando, FetchWhy::Chosen) == FetchAnswer::Fetching);
    f.run();
    // Both checks fail here, and the specification does not say which is made first.
    const bool mismatch = f.heard.events == Events{"failed " + kCommando + " MSG_FETCH_MISMATCH"};
    const bool invalid = f.heard.events == Events{"failed " + kCommando + " MSG_INVALID_SID"};
    CHECK((mismatch || invalid));
    CHECK(snapshot(f.root) == before);
    CHECK(f.arrivals.paths.empty());
}

TEST_CASE("SPEC-141 SPEC-069 each way a request can fail gives its message, and leaves no file") {
    struct Row {
        const char* what;
        std::function<void(Fetch*)> make;
        const char* message;
    };
    const std::vector<Row> rows = {
        {"the server has no such file (404)", [](Fetch* f) { f->status(kCommando, 404); }, "MSG_FETCH_NOT_FOUND"},
        {"the server has no such file (410)", [](Fetch* f) { f->status(kCommando, 410); }, "MSG_FETCH_NOT_FOUND"},
        {"a server error", [](Fetch* f) { f->status(kCommando, 500); }, "MSG_FETCH_NETWORK"},
        {"no connection, or a timeout", [](Fetch* f) { f->fail_with(kCommando, spz::HttpFailure::Network); }, "MSG_FETCH_NETWORK"},
        {"the certificate is refused", [](Fetch* f) { f->fail_with(kCommando, spz::HttpFailure::Tls); }, "MSG_FETCH_NETWORK"},
        {"the network is down", [](Fetch* f) { f->http.down = true; }, "MSG_FETCH_NETWORK"},
        {"the answer is cut off", [](Fetch* f) {
             spz::fake::HttpClient::Answer cut;
             cut.body = fixture("tunes/commando_test.sid");
             cut.cut_after = 40;
             f->http.answer(Fetch::url(kCommando), cut);
         }, "MSG_FETCH_NETWORK"},
        {"more than 1 MiB", [](Fetch* f) { f->answer_with(kCommando, fixture("tunes/bad_too_large.sid")); }, "MSG_FETCH_TOO_LARGE"},
        {"the card fills up", [](Fetch* f) { f->fail_with(kCommando, spz::HttpFailure::CardFull); }, "MSG_CARD_FULL"},
        {"the file cannot be written", [](Fetch* f) { f->fail_with(kCommando, spz::HttpFailure::CannotWrite); }, "MSG_CANNOT_WRITE"},
    };
    for (const Row& row : rows) {
        CAPTURE(row.what);
        Fetch f;
        const auto before = snapshot(f.root);
        row.make(&f);
        REQUIRE(f.request(kCommando, FetchWhy::Chosen) == FetchAnswer::Fetching);
        f.run();
        CHECK(f.http.log.size() == 1);
        CHECK(f.heard.events == Events{"failed " + kCommando + " " + row.message});
        CHECK(snapshot(f.root) == before);
        CHECK(f.arrivals.paths.empty());
    }
}

TEST_CASE("SPEC-141 SPEC-069 a tune whose folder cannot be made fails with MSG_CANNOT_WRITE") {
    Fetch f;
    f.files.make_read_only(f.root + "/MUSICIANS");
    REQUIRE(f.request(kCommando, FetchWhy::Chosen) == FetchAnswer::Fetching);
    f.run();
    CHECK(f.heard.events == Events{"failed " + kCommando + " MSG_CANNOT_WRITE"});
    CHECK_FALSE(exists(f.in_root(kCommando)));
}

TEST_CASE("SPEC-141 with 7 MiB free no request is made and the fetch fails with MSG_CARD_FULL; with 8 MiB it is made") {
    {
        Fetch f;
        f.files.set_free_bytes(7 * kMiB);
        REQUIRE(f.request(kCommando, FetchWhy::Chosen) == FetchAnswer::Fetching);
        f.run();
        CHECK(f.http.calls == 0);
        CHECK(f.heard.events == Events{"failed " + kCommando + " MSG_CARD_FULL"});
        CHECK_FALSE(exists(f.in_root(kCommando)));
    }
    {
        Fetch f;
        f.files.set_free_bytes(8 * kMiB);
        REQUIRE(f.request(kCommando, FetchWhy::Chosen) == FetchAnswer::Fetching);
        f.run();
        CHECK(f.http.log.size() == 1);
        CHECK(f.heard.events == Events{"fetched " + kCommando});
    }
}

TEST_CASE("SPEC-141 next pressed 50 times within five seconds makes one request, 700 ms after the last press, for the entry landed on") {
    Fetch f;
    const std::string landed = kTunes85[49 % kTunes85.size()];
    for (int press = 0; press < 50; ++press) {
        // Each step lands on an absent tune, 100 ms after the one before.
        REQUIRE(f.request(kTunes85[static_cast<std::size_t>(press) % kTunes85.size()], FetchWhy::Stepped) == FetchAnswer::Waiting);
        if (press < 49) {
            f.clock.advance(100);
            f.run();
        }
    }
    CHECK(f.http.calls == 0);

    f.clock.advance(699);
    f.run();
    CHECK(f.http.calls == 0);
    CHECK(f.heard.events.empty());

    f.clock.advance(1);
    f.run();
    CHECK(urls(f.http) == Events{Fetch::url(landed)});
    CHECK(f.heard.events == Events{"started " + landed, "fetched " + landed});
    CHECK(exists(f.in_root(landed)));

    // And nothing more comes of the 49 steps before it.
    f.clock.advance(10 * 1000);
    f.run();
    CHECK(f.http.log.size() == 1);
}

TEST_CASE("SPEC-141 SPEC-060 a wait that is ended by Stop, or replaced by Enter on a tune, never makes its own request") {
    Fetch f;
    REQUIRE(f.request(kCommando, FetchWhy::Stepped) == FetchAnswer::Waiting);
    f.clock.advance(300);
    f.fetcher->cancel_play();                      // Stop, in Waiting
    f.clock.advance(5000);
    f.run();
    CHECK(f.http.calls == 0);
    CHECK(f.heard.events.empty());

    REQUIRE(f.request(kCommando, FetchWhy::Stepped) == FetchAnswer::Waiting);
    f.clock.advance(300);
    CHECK(f.request(kThreeSongs, FetchWhy::Chosen) == FetchAnswer::Fetching);    // Enter on another tune: at once
    f.run();
    CHECK(urls(f.http) == Events{Fetch::url(kThreeSongs)});
    f.clock.advance(5000);
    f.run();
    CHECK(f.http.log.size() == 1);
    CHECK_FALSE(exists(f.in_root(kCommando)));
}

TEST_CASE("SPEC-141 the fetch-ahead is asked for when 10 s or less remain of a song that has a length, and not before") {
    // A 30-second song.
    CHECK_FALSE(spz::fetch_ahead_due(30000, 0));
    CHECK_FALSE(spz::fetch_ahead_due(30000, 19999));
    CHECK(spz::fetch_ahead_due(30000, 20000));
    CHECK(spz::fetch_ahead_due(30000, 29999));
    // A two-second song has less than that to go from its first sample.
    CHECK(spz::fetch_ahead_due(2000, 0));
    CHECK(spz::fetch_ahead_due(10000, 0));
    CHECK_FALSE(spz::fetch_ahead_due(10001, 0));
    // A song with no length never has one.
    CHECK_FALSE(spz::fetch_ahead_due(0, 0));
    CHECK_FALSE(spz::fetch_ahead_due(0, 3600 * 1000));
}

TEST_CASE("SPEC-141 a fetch-ahead that is answered in time leaves the entry present when its turn comes, and tells the player only that") {
    Fetch f;
    REQUIRE(f.request(kCommando, FetchWhy::Ahead) == FetchAnswer::Fetching);
    f.run();
    CHECK(urls(f.http) == Events{Fetch::url(kCommando)});
    CHECK(f.heard.events == Events{"fetched " + kCommando});        // no "started": the player's state is not touched
    REQUIRE(f.heard.whys.size() == 1);
    CHECK(f.heard.whys[0] == FetchWhy::Ahead);
    CHECK(exists(f.in_root(kCommando)));

    // The song ends and the entry's turn comes: it is simply present.
    CHECK(f.request(kCommando, FetchWhy::Unattended) == FetchAnswer::Present);
    f.run();
    CHECK(f.http.log.size() == 1);
}

TEST_CASE("SPEC-141 a fetch-ahead that failed fails the entry at the song's end with that failure, without a second request") {
    Fetch f;
    f.http.down = true;
    REQUIRE(f.request(kCommando, FetchWhy::Ahead) == FetchAnswer::Fetching);
    f.run();
    CHECK(f.heard.events == Events{"failed " + kCommando + " MSG_FETCH_NETWORK"});
    CHECK(f.heard.count("started") == 0);
    CHECK(f.fetcher->ahead_failure(kCommando) == "MSG_FETCH_NETWORK");
    CHECK(f.fetcher->ahead_failure(kThreeSongs) == "");
    f.http.down = false;                          // even if the network is back by then

    CHECK(f.request(kCommando, FetchWhy::Unattended) == FetchAnswer::Failed);
    f.run();
    CHECK(f.http.log.size() == 1);
    CHECK(f.fetcher->ahead_failure(kCommando) == "MSG_FETCH_NETWORK");

    // A key is another matter: the tune is requested again.
    CHECK(f.request(kCommando, FetchWhy::Chosen) == FetchAnswer::Fetching);
    f.run();
    CHECK(f.http.log.size() == 2);
    CHECK(exists(f.in_root(kCommando)));
}

TEST_CASE("SPEC-141 a fetch-ahead held back past the song's end is taken over: the player waits for that same request, and there is still one") {
    for (const FetchWhy first : {FetchWhy::Ahead, FetchWhy::Chosen}) {
        for (const FetchWhy second : {FetchWhy::Unattended, FetchWhy::Chosen}) {
            CAPTURE(static_cast<int>(first));
            CAPTURE(static_cast<int>(second));
            Fetch f;
            f.hold(kCommando);
            REQUIRE(f.request(kCommando, first) == FetchAnswer::Fetching);
            {
                HeldWorker worker(&f.http, {Fetch::url(kCommando)});
                worker.start(&f.fetch_worker);
                REQUIRE(worker.wait_for_stalled());
                CHECK(f.http.log.size() == 1);

                // The song ends (or Enter is pressed on the same tune) while the request is in flight.
                CHECK(f.request(kCommando, second) == FetchAnswer::Fetching);
                CHECK(f.fetcher->fetching(kCommando));
                worker.release_and_join();
            }
            f.run();
            CHECK(urls(f.http) == Events{Fetch::url(kCommando)});
            CHECK(f.heard.events == Events{"fetched " + kCommando});
            REQUIRE(f.heard.whys.size() == 1);
            CHECK(f.heard.whys[0] == second);
            CHECK(read_file(f.in_root(kCommando)) == fixture("tunes/commando_test.sid"));
        }
    }
}

TEST_CASE("SPEC-141 a request for the tune to be played cancels a fetch-ahead, or an earlier such request, that is in flight for another tune") {
    for (const FetchWhy first : {FetchWhy::Ahead, FetchWhy::Chosen, FetchWhy::Unattended}) {
        CAPTURE(static_cast<int>(first));
        Fetch f;
        const auto before = snapshot(f.root);
        f.hold(kCommando);
        REQUIRE(f.request(kCommando, first) == FetchAnswer::Fetching);
        {
            HeldWorker worker(&f.http, {Fetch::url(kCommando)});
            worker.start(&f.fetch_worker);
            REQUIRE(worker.wait_for_stalled());

            // Enter on a third tune meanwhile.
            CHECK(f.request(kThreeSongs, FetchWhy::Chosen) == FetchAnswer::Fetching);
            CHECK(worker.wait_for_end());         // the held request is cancelled; the new one follows it
        }
        f.run();
        CHECK(urls(f.http) == Events{Fetch::url(kCommando), Fetch::url(kThreeSongs)});
        CHECK(f.heard.count("fetched " + kCommando) == 0);
        CHECK(f.heard.count("fetched " + kThreeSongs) == 1);
        auto expected = before;
        expected[kThreeSongs.substr(1)] = fixture("tunes/three_subtunes.sid");
        CHECK(snapshot(f.root) == expected);      // nothing of the cancelled fetch is left
    }
}

TEST_CASE("SPEC-141 SPEC-060 Stop cancels the request that is in flight for the tune to be played") {
    Fetch f;
    const auto before = snapshot(f.root);
    f.hold(kCommando);
    REQUIRE(f.request(kCommando, FetchWhy::Chosen) == FetchAnswer::Fetching);
    {
        HeldWorker worker(&f.http, {Fetch::url(kCommando)});
        worker.start(&f.fetch_worker);
        REQUIRE(worker.wait_for_stalled());
        f.fetcher->cancel_play();
        CHECK(worker.wait_for_end());
    }
    f.run();
    CHECK(f.http.log.size() == 1);
    CHECK(f.heard.count("fetched") == 0);
    CHECK(snapshot(f.root) == before);
    CHECK_FALSE(f.fetcher->fetching(kCommando));
}

TEST_CASE("SPEC-141 SPEC-022 a tune that is to be played goes ahead of everything waiting on the fetch worker") {
    Fetch f;
    // Two files of "Fetch missing" are waiting, and none is in flight, when Enter is pressed on a tune.
    REQUIRE(f.request(kTunes85[0], FetchWhy::Playlist) == FetchAnswer::Fetching);
    CHECK(f.request(kTunes85[1], FetchWhy::Playlist) == FetchAnswer::Fetching);
    CHECK(f.request(kCommando, FetchWhy::Chosen) == FetchAnswer::Fetching);
    f.run();
    CHECK(urls(f.http) == Events{Fetch::url(kCommando), Fetch::url(kTunes85[0]), Fetch::url(kTunes85[1])});
    CHECK(f.heard.count("fetched") == 3);
}

TEST_CASE("SPEC-141 SPEC-174 a file of Fetch missing that is in flight is left to finish before the tune to be played") {
    Fetch f;
    f.hold(kTunes85[0]);
    REQUIRE(f.request(kTunes85[0], FetchWhy::Playlist) == FetchAnswer::Fetching);
    CHECK(f.request(kTunes85[1], FetchWhy::Playlist) == FetchAnswer::Fetching);
    {
        HeldWorker worker(&f.http, {Fetch::url(kTunes85[0])});
        worker.start(&f.fetch_worker);
        REQUIRE(worker.wait_for_stalled());
        CHECK(f.request(kCommando, FetchWhy::Chosen) == FetchAnswer::Fetching);
        worker.release_and_join();
    }
    f.run();
    // One at a time: the file in flight, then the tune to be played, then the job's next file.
    CHECK(urls(f.http) == Events{Fetch::url(kTunes85[0]), Fetch::url(kCommando), Fetch::url(kTunes85[1])});
    CHECK(exists(f.in_root(kTunes85[0])));
    CHECK(exists(f.in_root(kCommando)));
    CHECK(exists(f.in_root(kTunes85[1])));
    CHECK(f.heard.count("fetched") == 3);
}

TEST_CASE("SPEC-141 unattended requests are limited to six in any minute; the seventh waits for the next one allowed") {
    Fetch f;
    for (std::size_t song = 0; song < 6; ++song) {
        // Two-second songs: every two seconds playback reaches another absent tune.
        CAPTURE(song);
        REQUIRE(f.request(kTunes85[song], FetchWhy::Unattended) == FetchAnswer::Fetching);
        f.run();
        f.clock.advance(2000);
    }
    CHECK(f.http.log.size() == 6);

    // Twelve seconds after the first request: the seventh is not made, and the player waits.
    CHECK(f.request(kTunes85[6], FetchWhy::Unattended) == FetchAnswer::Waiting);
    f.run();
    CHECK(f.http.log.size() == 6);
    CHECK(f.heard.count("started") == 0);

    f.clock.advance(47 * 1000);                    // 59 s after the first request
    f.run();
    CHECK(f.http.log.size() == 6);

    f.clock.advance(2 * 1000);                     // 61 s: the first is more than a minute old
    f.run();
    REQUIRE(f.http.log.size() == 7);
    CHECK(f.http.log[6].url == Fetch::url(kTunes85[6]));
    CHECK(f.heard.count("started " + kTunes85[6]) == 1);
    CHECK(exists(f.in_root(kTunes85[6])));
}

TEST_CASE("SPEC-141 with two-second songs and absent tunes, no more than six requests are made in any minute, and they go on being made") {
    Fetch f;
    for (const std::string& path : kTunes85) f.status(path, 404);   // every tune stays absent
    std::vector<std::int64_t> made_at;
    std::size_t song = 0;
    for (int second = 0; second < 180; ++second) {
        // Every two seconds a song ends and playback reaches another absent tune.
        if (second % 2 == 0) f.fetcher->request(kTunes85[song++ % kTunes85.size()], FetchWhy::Unattended);
        f.run();
        while (made_at.size() < f.http.log.size()) made_at.push_back(second);
        f.clock.advance(1000);
        f.run();                                    // a request that became allowed meanwhile is made now
        while (made_at.size() < f.http.log.size()) made_at.push_back(second + 1);
    }
    REQUIRE(made_at.size() >= 7);
    for (std::size_t last = 0; last < made_at.size(); ++last) {
        int in_the_minute = 0;
        for (std::size_t other = 0; other <= last; ++other) {
            if (made_at[other] > made_at[last] - 60) ++in_the_minute;
        }
        CAPTURE(made_at[last]);
        CHECK(in_the_minute <= 6);
    }
    CHECK(made_at.size() >= 12);                   // three minutes: the limit holds requests back, it does not end them
}

TEST_CASE("SPEC-141 a fetch-ahead counts as an unattended request, and one beyond the limit is not started") {
    Fetch f;
    for (std::size_t song = 0; song < 6; ++song) {
        CAPTURE(song);
        REQUIRE(f.request(kTunes85[song], FetchWhy::Ahead) == FetchAnswer::Fetching);
        f.run();
        f.clock.advance(2000);
    }
    CHECK(f.request(kTunes85[6], FetchWhy::Ahead) == FetchAnswer::OverLimit);
    f.run();
    CHECK(f.http.log.size() == 6);
    // When that entry's turn comes, the player waits in Waiting for the next request allowed.
    CHECK(f.request(kTunes85[6], FetchWhy::Unattended) == FetchAnswer::Waiting);
    f.run();
    CHECK(f.http.log.size() == 6);
}

TEST_CASE("SPEC-141 requests that a key asks for are not limited") {
    Fetch f;
    for (std::size_t press = 0; press < 8; ++press) {
        CAPTURE(press);
        REQUIRE(f.request(kTunes85[press], FetchWhy::Chosen) == FetchAnswer::Fetching);
        f.run();
        f.clock.advance(1000);
    }
    CHECK(f.http.log.size() == 8);
    CHECK(f.heard.count("fetched") == 8);
}

TEST_CASE("SPEC-143 for 60 s after an unattended fetch failed for lack of network, unattended playback makes no request") {
    for (const FetchWhy failed_as : {FetchWhy::Ahead, FetchWhy::Unattended}) {
        CAPTURE(static_cast<int>(failed_as));
        Fetch f;
        f.http.down = true;
        REQUIRE(f.request(kTunes85[2], failed_as) == FetchAnswer::Fetching);
        f.run();
        REQUIRE(f.heard.events == Events{"failed " + kTunes85[2] + " MSG_FETCH_NETWORK"});
        f.http.down = false;

        // The next absent entries are passed over at once: no request, and no fetch-ahead either.
        f.clock.advance(2000);
        CHECK(f.request(kTunes85[3], FetchWhy::Unattended) == FetchAnswer::Resting);
        CHECK(f.request(kTunes85[4], FetchWhy::Ahead) == FetchAnswer::Resting);
        f.run();
        f.clock.advance(57 * 1000);                // 59 s after the failure
        CHECK(f.request(kTunes85[3], FetchWhy::Unattended) == FetchAnswer::Resting);
        f.run();
        CHECK(f.http.log.size() == 1);
        CHECK(f.heard.events.size() == 1);

        f.clock.advance(2 * 1000);                 // 61 s
        CHECK(f.request(kTunes85[3], FetchWhy::Unattended) == FetchAnswer::Fetching);
        f.run();
        REQUIRE(f.http.log.size() == 2);
        CHECK(f.http.log[1].url == Fetch::url(kTunes85[3]));
        CHECK(exists(f.in_root(kTunes85[3])));
    }
}

TEST_CASE("SPEC-143 with the network down and 30-second songs, the queue present, absent, absent, present costs one failed request, made when 10 s remained") {
    // What the player asks of the fetcher while that queue plays unattended, moment by moment.
    Fetch f;
    f.http.down = true;
    const std::string second = kTunes85[2], third = kTunes85[3];

    // The first entry is present and plays. With 10 s of its 30 s to go, the fetch-ahead is due.
    f.clock.advance(19999);
    CHECK_FALSE(spz::fetch_ahead_due(30000, 19999));
    f.run();
    CHECK(f.http.calls == 0);
    f.clock.advance(1);
    REQUIRE(spz::fetch_ahead_due(30000, 20000));
    REQUIRE(f.request(second, FetchWhy::Ahead) == FetchAnswer::Fetching);
    f.run();
    CHECK(urls(f.http) == Events{Fetch::url(second)});
    CHECK(f.heard.events == Events{"failed " + second + " MSG_FETCH_NETWORK"});

    // The song ends: the second entry fails with that failure, and is not requested again.
    f.clock.advance(10000);
    CHECK(f.request(second, FetchWhy::Unattended) == FetchAnswer::Failed);
    CHECK(f.fetcher->ahead_failure(second) == "MSG_FETCH_NETWORK");
    // Its failure has been shown for 2 s. The third entry is absent too: it is passed over at once,
    // without a request, and so the fourth, which is present, plays.
    f.clock.advance(2000);
    CHECK(f.request(third, FetchWhy::Unattended) == FetchAnswer::Resting);
    f.run();
    CHECK(f.http.log.size() == 1);
    CHECK(f.heard.events.size() == 1);
    CHECK(f.panel.opened.empty());
}

TEST_CASE("SPEC-143 within the 60 s a tune chosen by a key is still requested, and its failure is told") {
    Fetch f;
    f.http.down = true;
    REQUIRE(f.request(kTunes85[2], FetchWhy::Ahead) == FetchAnswer::Fetching);
    f.run();
    REQUIRE(f.http.log.size() == 1);

    f.clock.advance(5000);
    CHECK(f.request(kTunes85[3], FetchWhy::Chosen) == FetchAnswer::Fetching);    // Enter
    f.run();
    REQUIRE(f.http.log.size() == 2);
    CHECK(f.http.log[1].url == Fetch::url(kTunes85[3]));
    CHECK(f.heard.events.back() == "failed " + kTunes85[3] + " MSG_FETCH_NETWORK");

    CHECK(f.request(kTunes85[4], FetchWhy::Stepped) == FetchAnswer::Waiting);    // a next or previous key
    f.clock.advance(700);
    f.run();
    REQUIRE(f.http.log.size() == 3);
    CHECK(f.http.log[2].url == Fetch::url(kTunes85[4]));
}

TEST_CASE("SPEC-143 SPEC-069 the failures for lack of network, room or a place to write start the 60 s; a file that is wrong or not there does not") {
    struct Row {
        const char* what;
        std::function<void(Fetch*, const std::string&)> make;
        const char* message;
        bool rests;
    };
    const std::vector<Row> rows = {
        {"no network", [](Fetch* f, const std::string& path) { f->fail_with(path, spz::HttpFailure::Network); }, "MSG_FETCH_NETWORK", true},
        {"a server error", [](Fetch* f, const std::string& path) { f->status(path, 503); }, "MSG_FETCH_NETWORK", true},
        {"too large", [](Fetch* f, const std::string& path) { f->answer_with(path, fixture("tunes/bad_too_large.sid")); }, "MSG_FETCH_TOO_LARGE", true},
        {"the card is full", [](Fetch* f, const std::string& path) { f->fail_with(path, spz::HttpFailure::CardFull); }, "MSG_CARD_FULL", true},
        {"it cannot be written", [](Fetch* f, const std::string& path) { f->fail_with(path, spz::HttpFailure::CannotWrite); }, "MSG_CANNOT_WRITE", true},
        {"not on the server", [](Fetch* f, const std::string& path) { f->status(path, 404); }, "MSG_FETCH_NOT_FOUND", false},
        {"not HVSC's file", [](Fetch* f, const std::string& path) { f->answer_with(path, fixture("tunes/two_sid.sid")); }, "MSG_FETCH_MISMATCH", false},
    };
    for (const Row& row : rows) {
        for (const FetchWhy failed_as : {FetchWhy::Ahead, FetchWhy::Unattended}) {
            CAPTURE(row.what);
            CAPTURE(static_cast<int>(failed_as));
            Fetch f;
            row.make(&f, kTunes85[2]);
            REQUIRE(f.request(kTunes85[2], failed_as) == FetchAnswer::Fetching);
            f.run();
            REQUIRE(f.heard.events == Events{"failed " + kTunes85[2] + " " + row.message});

            f.clock.advance(1000);
            const FetchAnswer next = f.request(kTunes85[3], FetchWhy::Unattended);
            f.run();
            if (row.rests) {
                CHECK(next == FetchAnswer::Resting);
                CHECK(f.http.log.size() == 1);
            } else {
                CHECK(next == FetchAnswer::Fetching);
                CHECK(f.http.log.size() == 2);
            }
        }
    }
}

TEST_CASE("SPEC-143 a fetch that a key asked for and that failed does not hold unattended requests back") {
    Fetch f;
    f.http.down = true;
    REQUIRE(f.request(kTunes85[2], FetchWhy::Chosen) == FetchAnswer::Fetching);
    f.run();
    REQUIRE(f.heard.events == Events{"failed " + kTunes85[2] + " MSG_FETCH_NETWORK"});
    f.http.down = false;

    f.clock.advance(1000);
    CHECK(f.request(kTunes85[3], FetchWhy::Unattended) == FetchAnswer::Fetching);
    f.run();
    CHECK(f.http.log.size() == 2);
}

TEST_CASE("SPEC-154 a path the listing does not name, or that is not an HVSC path, is never requested and nothing is written for it") {
    Fetch f;
    const std::vector<std::string> paths = {
        "/MUSICIANS/H/Hubbard_Rob/Not_Listed.sid",
        "/My Rips/Cool S\xC3\xB6ng.sid",
        "/MUSICIANS/../../outside.sid",
        "/MUSICIANS/H/Hubbard_Rob/../../../../escape.sid",
        "/MUSICIANS//Commando.sid",
        "MUSICIANS/H/Hubbard_Rob/Commando.sid",
        "",
    };
    const auto before = snapshot(f.temp.path);
    for (const std::string& path : paths) {
        CAPTURE(path);
        f.answer_with(path, fixture("tunes/commando_test.sid"));    // the server would give it, if asked
        const std::size_t changes_before = changes(f.files).size();
        const std::size_t events_before = f.heard.events.size();
        f.fetcher->request(path, FetchWhy::Chosen);
        f.run();
        CHECK(f.http.calls == 0);
        CHECK(changes(f.files).size() == changes_before);           // no write, rename, remove or new folder is tried
        CHECK(f.heard.count("fetched") == 0);
        CHECK(f.heard.events.size() == events_before + 1);          // whoever asked is told that it failed
        CHECK(f.heard.count("failed " + path) >= 1);
    }
    CHECK(snapshot(f.temp.path) == before);
    CHECK(f.arrivals.paths.empty());
}

#if SPZ_PART_RELEASES
namespace {

struct Changed : spz::ChangedTunes {
    std::set<std::string> again;
    std::vector<std::string> done;
    bool fetch_again(const std::string& path) override { return again.count(path) > 0; }
    void fetched_again(const std::string& path) override {
        done.push_back(path);
        again.erase(path);
    }
};

}  // namespace

TEST_CASE("SPEC-148 SPEC-141 a present tune that an update found changed is requested once when it is played, and replaced [parts: releases]") {
    Changed changed;
    changed.again = {kChanged};
    Fetch f("", &changed);
    write_file(f.in_root(kChanged), fixture("tunes/changed_84.sid"));     // the file of the release before
    f.put_tune(85, kCommando);                                            // and a present tune that did not change

    REQUIRE(f.request(kChanged, FetchWhy::Chosen) == FetchAnswer::Fetching);
    f.run();
    CHECK(urls(f.http) == Events{Fetch::url(kChanged)});
    CHECK(read_file(f.in_root(kChanged)) == fixture("tunes/changed_85.sid"));
    CHECK(changed.done == Events{kChanged});
    CHECK(f.heard.events == Events{"fetched " + kChanged});

    // Played again: it is not requested again. Nor is a present tune that the update did not mark.
    f.fetcher->request(kChanged, FetchWhy::Chosen);
    f.fetcher->request(kCommando, FetchWhy::Chosen);
    f.run();
    CHECK(f.http.log.size() == 1);
    CHECK(f.heard.count("fetched " + kCommando) == 1);
}

TEST_CASE("SPEC-148 SPEC-141 when the fetcher cannot fetch a tune that an update found changed, it says the file on the device can be played [parts: releases]") {
    Changed changed;
    changed.again = {kChanged};
    Fetch f("", &changed);
    write_file(f.in_root(kChanged), fixture("tunes/changed_84.sid"));
    f.http.down = true;

    REQUIRE(f.request(kChanged, FetchWhy::Chosen) == FetchAnswer::Fetching);
    f.run();
    CHECK(f.http.log.size() == 1);
    CHECK(read_file(f.in_root(kChanged)) == fixture("tunes/changed_84.sid"));
    CHECK(f.heard.events == Events{"fetched " + kChanged});         // it can be loaded: no failure is shown
    CHECK(changed.done.empty());
    CHECK_FALSE(exists(temporary_of(f.in_root(kChanged))));
}
#endif
