// SPDX-License-Identifier: GPL-3.0-or-later
// Releases (SPEC-148), part `releases`: the check for a newer release, the update, and what the update
// does with the tunes that are on the device. The reader of hv_sids.txt is tested with the set-up
// (hvsc_setup_test.cpp), which needs it whether or not this part is built.
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "hvsc_support.h"
#include "spz/fetcher.h"
#include "spz/hvsc_setup.h"
#include "spz/listing.h"
#include "spz/releases.h"
#include "support.h"

#if SPZ_PART_RELEASES

using namespace spz::test::hvsc;
using spz::FetchAnswer;
using spz::FetchWhy;
using spz::test::bytes;
using spz::test::fixture;

namespace {

using Words = std::vector<std::string>;

const char* const kOwnTune = "My Rips/Cool S\xC3\xB6ng.sid";
const std::string kHvSids = kUrlFiles + "/DOCUMENTS/hv_sids.txt";

// HVSC is set up with the fixture documents of release 84 and every tune of it is on the device; the fake
// server offers release 85, in which one tune was renamed and one changed. The notice is accepted.
struct Updating : World {
    HvscEvents events;
    spz::Listing listing;                 // the listing in use: release 84's
    std::unique_ptr<spz::Releases> releases;

    Updating() {
        record.accepted = true;
        put_collection(84);
        if (!open_listing(&listing)) throw std::runtime_error("the fixture database gave no listing");
        serve_collection(&http, 85);
        releases = make();
    }

    // A Releases over this world: the one the test uses, or the one the app makes after a restart.
    std::unique_ptr<spz::Releases> make() {
        spz::ReleasesParts parts;
        parts.files = &files;
        parts.http = &http;
        parts.long_worker = &long_worker;
        parts.ui = &ui;
        parts.gate = &gate;
        parts.jobs = &jobs;
        parts.notices = &notices;
        parts.settings = &settings;
        parts.root = root;
        parts.cache = cache;
        parts.listener = &events;
        return std::unique_ptr<spz::Releases>(new spz::Releases(parts));
    }

    void offer(const std::string& hv_sids_text) {
        spz::fake::HttpClient::Answer answer;
        answer.body = bytes(hv_sids_text);
        http.answer(kHvSids, answer);
    }

    Words requested_sorted() const {
        Words list = urls(http);
        std::sort(list.begin(), list.end());
        return list;
    }
    static Words document_urls() {
        Words list;
        for (const std::string& document : kDocuments) list.push_back(kUrlFiles + "/" + document);
        std::sort(list.begin(), list.end());
        return list;
    }
};

}  // namespace

TEST_CASE("SPEC-148 with release 84 set up and a server offering 85, the check reports 85 and changes nothing [parts: releases]") {
    Updating u;
    const auto before = snapshot(u.root);
    CHECK(u.releases->newer() == 0);                  // nothing is known before the user asks

    u.releases->check();
    CHECK_FALSE(u.jobs.running());                    // a check is not a job
    CHECK(u.http.calls == 0);                         // and it is the long worker's to make
    u.run();

    REQUIRE(u.http.log.size() == 1);
    CHECK(u.http.log[0].url == "https://hvsc.c64.org/download/C64Music/DOCUMENTS/hv_sids.txt");
    CHECK(u.http.log[0].limit_bytes == 32 * kMiB);
    CHECK(u.http.log[0].resume_from == 0);
    CHECK(starts_with(u.http.log[0].to_path, u.cache + "/"));

    CHECK(u.releases->newer() == 85);                 // the row "Update to #85"
    CHECK(u.notices.ids() == Words{"MSG_HVSC_NEWER"});
    CHECK(u.notices.about("MSG_HVSC_NEWER", "m") == "85");
    CHECK(snapshot(u.root) == before);
    CHECK(spz::read_release(read_file(u.root + "/DOCUMENTS/hv_sids.txt")) == 84);
}

TEST_CASE("SPEC-148 a check that finds the same release, or a lower one, says that this is the newest [parts: releases]") {
    for (const char* offered : {"# stub for old updaters\r\nRelease 84\r\n", "# stub for old updaters\r\nRelease 83\r\n"}) {
        CAPTURE(offered);
        Updating u;
        u.offer(offered);
        u.releases->check();
        u.run();
        CHECK(u.http.log.size() == 1);
        CHECK(u.releases->newer() == 0);
        CHECK(u.notices.ids() == Words{"MSG_HVSC_NEWEST"});
        CHECK(u.notices.about("MSG_HVSC_NEWEST", "n") == "84");
    }
}

TEST_CASE("SPEC-148 a check whose file cannot be fetched, or names no release, says that it could not check [parts: releases]") {
    struct Fault {
        const char* what;
        std::function<void(Updating*)> make;
    };
    const std::vector<Fault> faults = {
        {"no network", [](Updating* u) { u->http.down = true; }},
        {"the file is not there", [](Updating* u) {
             spz::fake::HttpClient::Answer gone;
             gone.status = 404;
             u->http.answer(kHvSids, gone);
         }},
        {"the file names no release", [](Updating* u) { u->offer("# stub for old updaters\r\n"); }},
        {"the file is something else", [](Updating* u) { u->offer("<html><body>Please log in</body></html>\n"); }},
    };
    for (const Fault& fault : faults) {
        CAPTURE(fault.what);
        Updating u;
        const auto before = snapshot(u.root);
        fault.make(&u);
        u.releases->check();
        u.run();
        CHECK(u.http.log.size() == 1);
        CHECK(u.releases->newer() == 0);
        CHECK(u.notices.ids() == Words{"MSG_CHECK_FAILED"});
        CHECK(snapshot(u.root) == before);
    }
}

TEST_CASE("SPEC-148 after updating, the listing is release 85's, and a tune 85 moved is present at its new path with no request [parts: releases]") {
    Updating u;
    write_file(u.root + "/" + kOwnTune, fixture("tunes/own_tune.sid"));

    REQUIRE(u.releases->update());
    CHECK(u.jobs.running());
    CHECK(u.jobs.state().kind == spz::JobKind::Update);
    u.run();

    // SPEC-140's steps 2 to 6: exactly the five documents were requested, and no tune.
    CHECK(u.requested_sorted() == Updating::document_urls());
    for (const std::string& document : kDocuments) {
        CAPTURE(document);
        CHECK(read_file(u.root + "/" + document) == fixture(collection(85) + "/" + document));
    }
    CHECK(u.notices.ids() == Words{"MSG_HVSC_READY"});
    CHECK(u.notices.about("MSG_HVSC_READY", "n") == "85");
    CHECK(u.events.events == Words{"ready 85"});
    CHECK_FALSE(u.jobs.running());

    // What the browser lists from now on is what 85 lists.
    spz::Listing now;
    REQUIRE(now.open(&u.files, u.listing_file(), u.database()));
    Words listed;
    for (const spz::ListedTune& tune : now.tunes(0, 100)) listed.push_back(tune.path);
    CHECK(listed == kTunes85);
    CHECK(spz::hvsc_status(&u.files, now, u.root).release == 85);

    // The tune 85 moved: the file is at its new path, and not at the old one.
    CHECK(read_file(u.in_root(kRenamed)) == fixture("tunes/moved.sid"));
    CHECK_FALSE(exists(u.in_root(kOldName)));
    // A playlist's entry for it is found by its MD5 at the new path (SPEC-173).
    spz::ListedTune found;
    REQUIRE(now.by_md5(fixture_md5(84, kOldName), &found));
    CHECK(found.path == kRenamed);

    // Every other tune is where it was, and so is the user's own.
    for (const std::string& tune : kTunes85) CHECK(exists(u.in_root(tune)));
    CHECK(read_file(u.in_root(kCommando)) == fixture("tunes/commando_test.sid"));
    CHECK(read_file(u.root + "/" + kOwnTune) == fixture("tunes/own_tune.sid"));
    // The tune whose bytes changed is not touched by the update itself.
    CHECK(read_file(u.in_root(kChanged)) == fixture("tunes/changed_84.sid"));
}

TEST_CASE("SPEC-148 after updating, a tune whose bytes changed is requested once when it is played [parts: releases]") {
    Updating u;
    REQUIRE(u.releases->update());
    u.run();
    const std::size_t requests_of_update = u.http.log.size();
    spz::Listing now;
    REQUIRE(now.open(&u.files, u.listing_file(), u.database()));

    CHECK(u.releases->fetch_again(kChanged));
    CHECK_FALSE(u.releases->fetch_again(kCommando));
    CHECK_FALSE(u.releases->fetch_again(kRenamed));

    Heard heard;
    spz::FetcherParts parts = fetcher_parts(u, &now);
    parts.changed = u.releases.get();
    spz::Fetcher fetcher(parts);
    fetcher.set_listener(&heard);

    // A present tune that did not change is played with no request.
    fetcher.request(kCommando, FetchWhy::Chosen);
    u.run();
    CHECK(u.http.log.size() == requests_of_update);

    // The changed one is requested, and replaced by release 85's file.
    fetcher.request(kChanged, FetchWhy::Chosen);
    u.run();
    REQUIRE(u.http.log.size() == requests_of_update + 1);
    CHECK(u.http.log.back().url == kUrlFiles + kChanged);
    CHECK(read_file(u.in_root(kChanged)) == fixture("tunes/changed_85.sid"));
    CHECK(heard.count("fetched " + kChanged) == 1);
    CHECK_FALSE(u.releases->fetch_again(kChanged));

    // Once: played again, it is not requested again.
    fetcher.request(kChanged, FetchWhy::Chosen);
    u.run();
    CHECK(u.http.log.size() == requests_of_update + 1);
}

TEST_CASE("SPEC-148 a changed tune that the user had replaced with other bytes is not requested [parts: releases]") {
    Updating u;
    write_file(u.in_root(kChanged), fixture("tunes/own_tune.sid"));     // no longer the file release 84 had
    REQUIRE(u.releases->update());
    u.run();
    const std::size_t requests_of_update = u.http.log.size();
    spz::Listing now;
    REQUIRE(now.open(&u.files, u.listing_file(), u.database()));

    CHECK_FALSE(u.releases->fetch_again(kChanged));
    Heard heard;
    spz::FetcherParts parts = fetcher_parts(u, &now);
    parts.changed = u.releases.get();
    spz::Fetcher fetcher(parts);
    fetcher.set_listener(&heard);
    fetcher.request(kChanged, FetchWhy::Chosen);
    u.run();
    CHECK(u.http.log.size() == requests_of_update);
    CHECK(read_file(u.in_root(kChanged)) == fixture("tunes/own_tune.sid"));
    CHECK(heard.events == Words{"fetched " + kChanged});
}

TEST_CASE("SPEC-148 if the fetch of a changed tune fails, the file on the device is played [parts: releases]") {
    Updating u;
    REQUIRE(u.releases->update());
    u.run();
    const std::size_t requests_of_update = u.http.log.size();
    spz::Listing now;
    REQUIRE(now.open(&u.files, u.listing_file(), u.database()));

    Heard heard;
    spz::FetcherParts parts = fetcher_parts(u, &now);
    parts.changed = u.releases.get();
    spz::Fetcher fetcher(parts);
    fetcher.set_listener(&heard);
    u.http.down = true;
    fetcher.request(kChanged, FetchWhy::Chosen);
    u.run();
    CHECK(u.http.log.size() == requests_of_update + 1);
    CHECK(read_file(u.in_root(kChanged)) == fixture("tunes/changed_84.sid"));
    CHECK(heard.events == Words{"fetched " + kChanged});
}

TEST_CASE("SPEC-148 a tune whose path and MD5 the new listing does not have stays where it is, as a tune of the user's own [parts: releases]") {
    Updating u;
    // At the path that release 85 gives up, the user has put a tune of their own.
    write_file(u.in_root(kOldName), fixture("tunes/own_tune.sid"));
    write_file(u.root + "/" + kOwnTune, fixture("tunes/own_tune.sid"));
    REQUIRE(u.releases->update());
    u.run();

    CHECK(read_file(u.in_root(kOldName)) == fixture("tunes/own_tune.sid"));
    CHECK(read_file(u.root + "/" + kOwnTune) == fixture("tunes/own_tune.sid"));
    CHECK_FALSE(exists(u.in_root(kRenamed)));         // nothing was moved there: that tune is simply absent now
    CHECK_FALSE(u.releases->fetch_again(kOldName));
    CHECK(u.requested_sorted() == Updating::document_urls());
}

TEST_CASE("SPEC-148 what an update found changed is still known after the app has started again [parts: releases]") {
    Updating u;
    REQUIRE(u.releases->update());
    u.run();
    REQUIRE(u.releases->fetch_again(kChanged));

    u.releases.reset();
    std::unique_ptr<spz::Releases> restarted = u.make();
    CHECK(restarted->fetch_again(kChanged));
    CHECK_FALSE(restarted->fetch_again(kCommando));
    CHECK(restarted->newer() == 0);                   // and nothing checks for releases unless the user asks
    u.run();
    CHECK(u.http.log.size() == 5);
}

TEST_CASE("SPEC-148 SPEC-140 an update that fails leaves the root, the tunes and the listing as they were, and the notice names the step [parts: releases]") {
    Updating u;
    const auto before = snapshot(u.root);
    spz::fake::HttpClient::Answer gone;
    gone.status = 404;
    u.http.answer(kUrlFiles + "/DOCUMENTS/STIL.txt", gone);

    REQUIRE(u.releases->update());
    u.run();
    CHECK(snapshot(u.root) == before);
    CHECK(u.notices.ids() == Words{"MSG_HVSC_FAILED"});
    CHECK(u.notices.about("MSG_HVSC_FAILED", "step") == "3");
    CHECK(u.events.events == Words{"failed 3"});
    CHECK_FALSE(u.jobs.running());
    CHECK(exists(u.in_root(kOldName)));
    CHECK_FALSE(u.releases->fetch_again(kChanged));

    spz::Listing still;
    REQUIRE(still.open(&u.files, u.listing_file(), u.database()));
    spz::ListedTune tune;
    CHECK(still.by_path(kOldName, &tune));
}

TEST_CASE("SPEC-146 SPEC-148 an update needs 16 MiB as a set-up does: with 15 MiB free no request is made [parts: releases]") {
    Updating u;
    const auto before = snapshot(u.root);
    u.files.set_free_bytes(15 * kMiB);
    REQUIRE(u.releases->update());
    u.run();
    CHECK(u.http.calls == 0);
    CHECK(u.notices.count("MSG_NEED_ROOM") == 1);
    CHECK(u.notices.about("MSG_NEED_ROOM", "n") == "16");
    CHECK(u.notices.about("MSG_NEED_ROOM", "m") == "15");
    CHECK(snapshot(u.root) == before);
    CHECK_FALSE(u.jobs.running());
}

TEST_CASE("SPEC-148 SPEC-140 one job at a time: an update does not begin while another job runs [parts: releases]") {
    Updating u;
    REQUIRE(u.jobs.begin(spz::JobKind::Setup));
    CHECK_FALSE(u.releases->update());
    u.run();
    CHECK(u.http.calls == 0);
    CHECK(u.jobs.state().kind == spz::JobKind::Setup);
}

#endif  // SPZ_PART_RELEASES
