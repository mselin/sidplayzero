// SPDX-License-Identifier: GPL-3.0-or-later
// "Fetch missing" (SPEC-174): which requests the job makes, in which order and how far apart, and what
// stops it. The fetcher of SPEC-141 and the notice gate of SPEC-152 belong to other parts of `core`; here
// they are stood in for, the fetcher on top of the fake HttpClient so that the request log is its log.
#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "lists_support.h"
#include "spz/playlist_fetch.h"
#include "support.h"

using namespace spz::test;

#if SPZ_PART_PLAYLISTS

namespace {

const char* const kUrlFiles = "https://hvsc.c64.org/download/C64Music";   // the setting `url_files` (SPEC-130)

// The fetcher: one worker that takes requests as they come, as the fetch worker does (SPEC-022).
class FakeFetcher : public spz::TuneFetcher {
public:
    FakeFetcher(spz::fake::HttpClient* http, spz::fake::Clock* clock, const std::string& root)
        : http_(http), clock_(clock), root_(root) {}

    int calls = 0;
    std::vector<std::int64_t> asked_at;        // the fake clock's time at each request of the job
    std::function<void()> while_in_flight;     // called once, while the next file is in flight
    spz::fake::Executor worker;                // the fetch worker: the test runs its jobs

    void fetch_for_playlist(const std::string& hvsc_path, Done done) override {
        ++calls;
        asked_at.push_back(clock_->now_ms());
        worker.post([this, hvsc_path, done] { done(fetch_now(hvsc_path)); });
    }
    // What the player does when Enter is pressed on an absent tune: its request joins the same worker.
    void fetch_to_play(const std::string& hvsc_path) {
        worker.post([this, hvsc_path] { fetch_now(hvsc_path); });
    }

private:
    std::string fetch_now(const std::string& hvsc_path) {
        if (while_in_flight) {
            const std::function<void()> now = std::move(while_in_flight);
            while_in_flight = nullptr;
            now();
        }
        std::filesystem::create_directories(std::filesystem::path(root_ + hvsc_path).parent_path());
        spz::HttpRequest request;
        request.url = std::string(kUrlFiles) + hvsc_path;
        request.to_path = root_ + hvsc_path;
        request.limit_bytes = 1024 * 1024;
        const spz::HttpResult result = http_->fetch(request, nullptr);
        if (result.failure == spz::HttpFailure::None) return "";
        std::error_code ignored;
        std::filesystem::remove(root_ + hvsc_path, ignored);
        switch (result.failure) {
            case spz::HttpFailure::NotFound: return "MSG_FETCH_NOT_FOUND";
            case spz::HttpFailure::TooLarge: return "MSG_FETCH_TOO_LARGE";
            case spz::HttpFailure::CardFull: return "MSG_CARD_FULL";
            case spz::HttpFailure::CannotWrite: return "MSG_CANNOT_WRITE";
            default: return "MSG_FETCH_NETWORK";
        }
    }

    spz::fake::HttpClient* http_;
    spz::fake::Clock* clock_;
    std::string root_;
};

// The gate: the notice panel, and the two keys that close it.
class FakeGate : public spz::NoticeGate {
public:
    bool accepted = true;      // `notice_accepted`
    int calls = 0;
    int panels = 0;            // how often the notice panel was opened

    void pass(std::function<void()> go, std::function<void()> cancelled) override {
        ++calls;
        if (accepted) { go(); return; }
        ++panels;
        go_ = std::move(go);
        cancelled_ = std::move(cancelled);
    }
    void press_enter() {
        accepted = true;
        const std::function<void()> go = std::move(go_);
        go_ = nullptr; cancelled_ = nullptr;
        if (go) go();
    }
    void press_esc() {
        const std::function<void()> cancelled = std::move(cancelled_);
        go_ = nullptr; cancelled_ = nullptr;
        if (cancelled) cancelled();
    }

private:
    std::function<void()> go_, cancelled_;
};

class Watcher : public spz::FetchMissingListener {
public:
    std::vector<spz::FetchMissingState> progress;
    std::vector<spz::ListsResult> ended;
    void fetch_missing_progress(const spz::FetchMissingState& state) override { progress.push_back(state); }
    void fetch_missing_ended(const spz::ListsResult& notice) override { ended.push_back(notice); }
};

struct Rig {
    ListsWorld world;
    spz::fake::Clock clock;
    spz::fake::HttpClient http;
    FakeFetcher fetcher{&http, &clock, world.root};
    FakeGate gate;
    Watcher watcher;
    spz::FetchMissing job{&clock, &world.files, world.root, world.resolver.get(), &fetcher, &gate, &watcher};

    // Makes a tune of the fixture collection absent, and has the server offer it.
    void make_absent(const std::string& path) {
        http.answer(std::string(kUrlFiles) + path, file_text(world.root + path));
        world.take_away(path);
    }
    // Runs until the job says it is over: the worker's jobs as they come, and the clock in steps of
    // 10 ms while the job waits. Gives up after `limit_ms` of the fake clock.
    void run(std::int64_t limit_ms = 2000) {
        for (std::int64_t waited = 0; waited <= limit_ms; waited += 10) {
            fetcher.worker.run_all();
            if (!watcher.ended.empty()) return;
            clock.advance(10);
        }
    }
    std::string requests() const {
        std::string all;
        for (const spz::HttpRequest& request : http.log) all += request.url + "\n";
        return all;
    }
    spz::Found found(const std::string& path) { return world.resolver->resolve(spz::Entry{path, 0, ""}).found; }
};

std::string url(const std::string& path) { return std::string(kUrlFiles) + path + "\n"; }

std::vector<spz::Entry> three_absent(Rig* rig) {
    for (const char* path : {kClockDemo, kEitherChip, kHuelsbeck}) rig->make_absent(path);
    // A present tune and a missing one stand between them: neither is asked for.
    return {spz::Entry{kCommando, 0, ""}, spz::Entry{kClockDemo, 0, ""}, spz::Entry{kNothing, 0, ""},
            spz::Entry{kEitherChip, 0, ""}, spz::Entry{kHuelsbeck, 0, ""}};
}

}  // namespace

TEST_CASE("SPEC-174 a playlist with three absent entries makes three requests, in order, for those paths only, and all three become present [parts: playlists]") {
    Rig rig;
    const std::vector<spz::Entry> entries = three_absent(&rig);
    REQUIRE(rig.found(kClockDemo) == spz::Found::Absent);

    rig.job.start(entries);
    rig.run();

    CHECK(rig.requests() == url(kClockDemo) + url(kEitherChip) + url(kHuelsbeck));
    CHECK(rig.http.calls == 3);                                // counted by the fake: through the seam (SPEC-021)
    CHECK(rig.fetcher.calls == 3);
    for (const char* path : {kClockDemo, kEitherChip, kHuelsbeck}) {
        CAPTURE(path);
        CHECK(rig.found(path) == spz::Found::Present);
    }
    CHECK(rig.found(kNothing) == spz::Found::Missing);

    REQUIRE(rig.watcher.ended.size() == 1);                    // it ends with the notice
    CHECK(rig.watcher.ended[0].message == "MSG_FETCHED_SOME");
    CHECK(rig.watcher.ended[0].about["n"] == "3");
    CHECK(rig.watcher.ended[0].about["m"] == "0");             // none remain
    CHECK_FALSE(rig.job.state().running);
    CHECK(rig.gate.calls == 1);                                // it passed the gate first (SPEC-152)
}

TEST_CASE("SPEC-174 while it runs the job shows n of m, and between two files it pauses 250 ms [parts: playlists]") {
    Rig rig;
    const std::vector<spz::Entry> entries = three_absent(&rig);
    rig.job.start(entries);

    rig.fetcher.worker.run_all();                              // the first file is fetched
    CHECK(rig.http.log.size() == 1);
    CHECK(rig.job.state().running);
    CHECK(rig.job.state().total == 3);
    rig.clock.advance(240);                                    // not yet
    rig.fetcher.worker.run_all();
    CHECK(rig.http.log.size() == 1);
    rig.clock.advance(10);                                     // 250 ms after the first was over
    rig.fetcher.worker.run_all();
    CHECK(rig.http.log.size() == 2);
    rig.run();

    REQUIRE(rig.fetcher.asked_at.size() == 3);
    CHECK(rig.fetcher.asked_at[1] - rig.fetcher.asked_at[0] == 250);
    CHECK(rig.fetcher.asked_at[2] - rig.fetcher.asked_at[1] == 250);
    REQUIRE_FALSE(rig.watcher.progress.empty());
    int last_done = 0;
    for (const spz::FetchMissingState& state : rig.watcher.progress) {
        CHECK(state.total == 3);
        CHECK(state.done >= last_done);
        last_done = state.done;
    }
    CHECK(rig.watcher.progress.back().done == 3);              // "3 of 3"
}

TEST_CASE("SPEC-174 a playlist with 250 absent entries makes 200 requests, and the notice says 50 remain [parts: playlists]") {
    Rig rig;
    // 250 more tunes in HVSC's database, none of them on the device.
    std::string database = file_text(rig.world.root + "/DOCUMENTS/Songlengths.md5");
    std::vector<spz::Entry> entries;
    const std::string body = text(fixture("tunes/commando_test.sid"));
    for (int i = 1; i <= 250; ++i) {
        char path[64], md5[40];
        std::snprintf(path, sizeof path, "/GENERATED/Tune_%03d.sid", i);
        std::snprintf(md5, sizeof md5, "%032x", i);
        database += std::string("; ") + path + "\r\n" + md5 + "=0:02\r\n";
        entries.push_back(spz::Entry{path, 0, ""});
        rig.http.answer(std::string(kUrlFiles) + path, body);
    }
    rig.world.temp.write(std::filesystem::path(rig.world.root).filename().string() + "/DOCUMENTS/Songlengths.md5", database);
    rig.world.build_listing();
    REQUIRE(rig.found("/GENERATED/Tune_250.sid") == spz::Found::Absent);

    rig.job.start(entries);
    rig.run(120000);

    REQUIRE(rig.http.log.size() == 200);                       // at most 200 in one go
    for (int i = 0; i < 200; ++i) {
        char path[64];
        std::snprintf(path, sizeof path, "/GENERATED/Tune_%03d.sid", i + 1);
        CHECK(rig.http.log[static_cast<std::size_t>(i)].url == std::string(kUrlFiles) + path);
    }
    REQUIRE(rig.watcher.ended.size() == 1);
    CHECK(rig.watcher.ended[0].message == "MSG_FETCHED_SOME");
    CHECK(rig.watcher.ended[0].about["n"] == "200");
    CHECK(rig.watcher.ended[0].about["m"] == "50");
    CHECK(rig.found("/GENERATED/Tune_200.sid") == spz::Found::Present);
    CHECK(rig.found("/GENERATED/Tune_201.sid") == spz::Found::Absent);

    // A second go fetches the rest.
    rig.watcher.ended.clear();
    rig.job.start(entries);
    rig.run(120000);
    CHECK(rig.http.log.size() == 250);
    REQUIRE(rig.watcher.ended.size() == 1);
    CHECK(rig.watcher.ended[0].about["n"] == "50");
    CHECK(rig.watcher.ended[0].about["m"] == "0");
}

TEST_CASE("SPEC-174 Enter on another absent tune while the job runs puts that tune's request next in the log [parts: playlists]") {
    Rig rig;
    const std::vector<spz::Entry> entries = three_absent(&rig);
    rig.make_absent(kRenamed);
    // While the job's first file is in flight, the user plays another absent tune.
    rig.fetcher.while_in_flight = [&rig] { rig.fetcher.fetch_to_play(kRenamed); };

    rig.job.start(entries);
    rig.run();

    CHECK(rig.requests() == url(kClockDemo) + url(kRenamed) + url(kEitherChip) + url(kHuelsbeck));
    REQUIRE(rig.watcher.ended.size() == 1);
    CHECK(rig.watcher.ended[0].about["n"] == "3");             // the job's own three
}

TEST_CASE("SPEC-174 SPEC-152 with HVSC's notice not accepted the job asks first: no request until Enter, and none at all after ESC [parts: playlists]") {
    Rig rig;
    const std::vector<spz::Entry> entries = three_absent(&rig);
    rig.gate.accepted = false;

    rig.job.start(entries);
    rig.run();
    CHECK(rig.gate.panels == 1);                               // the notice panel is open
    CHECK(rig.http.log.empty());
    CHECK(rig.fetcher.calls == 0);

    rig.gate.press_esc();                                      // ESC cancels what was asked for
    rig.run();
    CHECK(rig.http.log.empty());
    CHECK_FALSE(rig.gate.accepted);
    CHECK_FALSE(rig.job.state().running);

    rig.watcher.ended.clear();
    rig.job.start(entries);
    CHECK(rig.gate.panels == 2);
    CHECK(rig.http.log.empty());
    rig.gate.press_enter();                                    // Enter accepts, and the job goes on
    rig.run();
    CHECK(rig.http.log.size() == 3);
    REQUIRE(rig.watcher.ended.size() == 1);
    CHECK(rig.watcher.ended[0].message == "MSG_FETCHED_SOME");
}

TEST_CASE("SPEC-174 with less than 8 MiB free on the card the job makes no request [parts: playlists]") {
    Rig rig;
    const std::vector<spz::Entry> entries = three_absent(&rig);
    rig.world.files.set_free_bytes(8u * 1024 * 1024 - 1);
    rig.job.start(entries);
    rig.run();
    CHECK(rig.http.log.empty());
    CHECK_FALSE(rig.job.state().running);
    CHECK(rig.world.files.count("free_bytes") >= 1);           // it asked the card, through the seam
    CHECK(rig.found(kClockDemo) == spz::Found::Absent);

    rig.watcher.ended.clear();
    rig.world.files.set_free_bytes(8u * 1024 * 1024);          // exactly enough
    rig.job.start(entries);
    rig.run();
    CHECK(rig.http.log.size() == 3);
}

TEST_CASE("SPEC-174 the job stops at the first failure for lack of network [parts: playlists]") {
    Rig rig;
    const std::vector<spz::Entry> entries = three_absent(&rig);
    spz::fake::HttpClient::Answer no_network;
    no_network.failure = spz::HttpFailure::Network;
    rig.http.answer(std::string(kUrlFiles) + kEitherChip, no_network);     // the second file cannot be reached

    rig.job.start(entries);
    rig.run();
    rig.clock.advance(5000);                                   // and nothing more happens later
    rig.fetcher.worker.run_all();

    CHECK(rig.requests() == url(kClockDemo) + url(kEitherChip));
    CHECK(rig.watcher.ended.size() == 1);
    CHECK_FALSE(rig.job.state().running);
    CHECK(rig.found(kClockDemo) == spz::Found::Present);
    CHECK(rig.found(kEitherChip) == spz::Found::Absent);
    CHECK(rig.found(kHuelsbeck) == spz::Found::Absent);
}

TEST_CASE("SPEC-174 a file the server does not have is left absent and the job goes on to the next [parts: playlists]") {
    Rig rig;
    const std::vector<spz::Entry> entries = three_absent(&rig);
    spz::fake::HttpClient::Answer gone;
    gone.status = 404;
    rig.http.answer(std::string(kUrlFiles) + kEitherChip, gone);

    rig.job.start(entries);
    rig.run();

    CHECK(rig.requests() == url(kClockDemo) + url(kEitherChip) + url(kHuelsbeck));
    REQUIRE(rig.watcher.ended.size() == 1);
    CHECK(rig.watcher.ended[0].message == "MSG_FETCHED_SOME");
    CHECK(rig.watcher.ended[0].about["n"] == "2");
    CHECK(rig.watcher.ended[0].about["m"] == "1");
    CHECK(rig.found(kEitherChip) == spz::Found::Absent);
    CHECK(rig.found(kHuelsbeck) == spz::Found::Present);
}

TEST_CASE("SPEC-174 Stop ends the job after the file in flight [parts: playlists]") {
    Rig rig;
    const std::vector<spz::Entry> entries = three_absent(&rig);
    rig.fetcher.while_in_flight = [&rig] { rig.job.stop(); };  // "Stop", and Enter on its question

    rig.job.start(entries);
    rig.run();
    rig.clock.advance(5000);
    rig.fetcher.worker.run_all();

    CHECK(rig.requests() == url(kClockDemo));                  // the file in flight was finished, and no other asked for
    CHECK(rig.found(kClockDemo) == spz::Found::Present);
    CHECK(rig.found(kEitherChip) == spz::Found::Absent);
    CHECK(rig.watcher.ended.size() == 1);
    CHECK_FALSE(rig.job.state().running);
}

#endif  // SPZ_PART_PLAYLISTS
