// SPDX-License-Identifier: GPL-3.0-or-later
// The network is used only when asked (SPEC-151): what this area's objects do by themselves, with a
// HttpClient that fails the test if it is called, and which addresses they ask for when they are told to.
// The whole session of the specification's oracle (start, browse, search, play, edit a playlist, change
// every setting) is the simulator's test of the same item.
#include <string>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "hvsc_support.h"
#include "spz/complete_download.h"
#include "spz/fetcher.h"
#include "spz/hvsc_setup.h"
#include "spz/listing.h"
#include "spz/releases.h"
#include "support.h"

using namespace spz::test::hvsc;
using spz::FetchWhy;

TEST_CASE("SPEC-151 starting with HVSC set up, reading what is known of it, and playing present tunes that the listing agrees with make no request") {
    World w;
    w.record.accepted = true;                          // even with the notice accepted
    w.put_collection(85);                              // every tune is on the device
    w.http.forbidden = true;                           // a HttpClient that fails the test if it is called

    spz::Listing listing;
    REQUIRE(w.open_listing(&listing));
    const spz::HvscStatus status = spz::hvsc_status(&w.files, listing, w.root);
    CHECK(status.set_up);
    CHECK(status.release == 85);

    spz::HvscSetup setup(setup_parts(w));              // "Get HVSC" exists, and is not chosen
    Heard heard;
    spz::Fetcher fetcher(fetcher_parts(w, &listing));
    fetcher.set_listener(&heard);
    for (const std::string& tune : kTunes85) {
        fetcher.request(tune, FetchWhy::Chosen);       // Enter on a present tune
        w.run();
        fetcher.request(tune, FetchWhy::Unattended);   // and reached again by a song ending
        w.run();
        w.clock.advance(2000);
    }
    w.clock.advance(10 * 60 * 1000);                   // ten minutes of nothing
    w.run();

    CHECK(w.http.forbidden_requests == 0);
    CHECK(w.http.calls == 0);
    CHECK(heard.count("fetched") >= static_cast<int>(kTunes85.size()));
    CHECK(heard.count("failed") == 0);
    CHECK_FALSE(w.jobs.running());
    CHECK(w.panel.opened.empty());
}

TEST_CASE("SPEC-151 with no collection at all, nothing is requested until Get HVSC is chosen") {
    World w;
    w.http.forbidden = true;
    spz::Listing none;
    const spz::HvscStatus status = spz::hvsc_status(&w.files, none, w.root);
    CHECK_FALSE(status.set_up);
    spz::HvscSetup setup(setup_parts(w));
    spz::Fetcher fetcher(fetcher_parts(w, &none));
    w.clock.advance(10 * 60 * 1000);
    w.run();
    CHECK(w.http.forbidden_requests == 0);
    CHECK(w.http.calls == 0);
    CHECK_FALSE(w.jobs.running());
}

TEST_CASE("SPEC-151 SPEC-165 what a set-up and a fetch request is under url_files, and nothing else is asked of any server") {
    World w;
    w.record.accepted = true;
    serve_collection(&w.http, 85);
    spz::HvscSetup setup(setup_parts(w));
    REQUIRE(setup.start());
    w.run();
    spz::Listing listing;
    REQUIRE(listing.open(&w.files, w.listing_file(), w.database()));
    spz::Fetcher fetcher(fetcher_parts(w, &listing));
    fetcher.request(kCommando, FetchWhy::Chosen);
    w.run();
    w.clock.advance(10 * 60 * 1000);
    w.run();

    REQUIRE(w.http.log.size() == 6);                   // five documents and the tune that was chosen
    for (const std::string& url : urls(w.http)) {
        CAPTURE(url);
        CHECK(starts_with(url, "https://hvsc.c64.org/download/C64Music/"));
        CHECK(url.find("/api/") == std::string::npos);
        CHECK(url.find('?') == std::string::npos);     // nothing about the user or the device rides along
    }
}

#if SPZ_PART_RELEASES
TEST_CASE("SPEC-151 SPEC-148 nothing checks for releases unless the user asks [parts: releases]") {
    World w;
    w.record.accepted = true;
    w.put_collection(84);
    w.http.forbidden = true;
    spz::Listing listing;
    REQUIRE(w.open_listing(&listing));

    spz::ReleasesParts parts;
    parts.files = &w.files; parts.http = &w.http; parts.long_worker = &w.long_worker; parts.ui = &w.ui;
    parts.gate = &w.gate; parts.jobs = &w.jobs; parts.notices = &w.notices; parts.settings = &w.settings;
    parts.root = w.root; parts.cache = w.cache;
    spz::Releases releases(parts);
    CHECK(releases.newer() == 0);
    CHECK_FALSE(releases.fetch_again(kChanged));

    // Playing every present tune with the release check built in asks for nothing either.
    spz::FetcherParts fetching = fetcher_parts(w, &listing);
    fetching.changed = &releases;
    spz::Fetcher fetcher(fetching);
    for (const spz::ListedTune& tune : listing.tunes(0, 100)) {
        fetcher.request(tune.path, FetchWhy::Chosen);
        w.run();
    }
    w.clock.advance(24 * 60 * 60 * 1000);              // a day goes by
    w.run();
    CHECK(w.http.forbidden_requests == 0);
    CHECK(w.http.calls == 0);
    CHECK(w.notices.raised.empty());
}
#endif

#if SPZ_PART_DOWNLOAD_ALL
namespace {

struct NoPlayer : spz::PlaySwitch {
    int calls = 0;
    void playing_off() override { ++calls; }
    void playing_on() override { ++calls; }
};

}  // namespace

TEST_CASE("SPEC-151 SPEC-144 the complete download asks for nothing until its row is chosen, not even at start [parts: download-all]") {
    World w;
    w.record.accepted = true;
    w.put_collection(85);
    w.http.forbidden = true;
    spz::Listing listing;
    REQUIRE(w.open_listing(&listing));
    spz::fake::Unpacker unpacker;
    spz::fake::Memory memory;
    NoPlayer play;

    spz::CompleteDownloadParts parts;
    parts.files = &w.files; parts.http = &w.http; parts.unpacker = &unpacker; parts.memory = &memory;
    parts.long_worker = &w.long_worker; parts.ui = &w.ui; parts.gate = &w.gate; parts.jobs = &w.jobs;
    parts.notices = &w.notices; parts.settings = &w.settings; parts.root = w.root; parts.cache = w.cache;
    parts.play = &play;
    spz::CompleteDownload download(parts);
    download.recover();                                // what the app does at every start
    CHECK_FALSE(download.unfinished());

    // The HVSC screen opens and works out whether to offer the row.
    w.files.set_free_bytes(4096 * kMiB);
    const spz::DownloadOffer offer =
        spz::download_offer(spz::hvsc_status(&w.files, listing, w.root), &w.files, &unpacker, &memory, w.root);
    CHECK(offer.offered);
    w.clock.advance(10 * 60 * 1000);
    w.run();

    CHECK(w.http.forbidden_requests == 0);
    CHECK(w.http.calls == 0);
    CHECK(unpacker.log.empty());                       // and 7z is not run either
    CHECK(play.calls == 0);
    CHECK_FALSE(w.jobs.running());
}
#endif
