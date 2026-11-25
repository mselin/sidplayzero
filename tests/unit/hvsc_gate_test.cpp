// SPDX-License-Identifier: GPL-3.0-or-later
// The notice, and the gate (SPEC-152): no request while `notice_accepted` is off; a key press that would
// make the first one opens the notice panel; Enter accepts and goes on, ESC cancels what was asked for.
// The ways in that belong to a part (SPEC-144, SPEC-148) are at the end, each a test of its own.
#include <string>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "hvsc_support.h"
#include "spz/complete_download.h"
#include "spz/fetcher.h"
#include "spz/hvsc_setup.h"
#include "spz/notice_gate.h"
#include "spz/releases.h"
#include "support.h"

using namespace spz::test::hvsc;

namespace {

// The words of SPEC-152, copied from the specification.
const char* const kNotice =
    "SidPlayZero can fetch music from the High Voltage SID Collection (HVSC), a free collection kept by the "
    "HVSC crew at hvsc.c64.org. HVSC says of its tunes: \"All of the tunes available in the High Voltage SID "
    "Collection (HVSC) are copyrighted.\" and \"Use of the SIDs in this collection for any purpose other than "
    "private enjoyment is subject to relevant copyright law.\" SidPlayZero downloads files to this device only "
    "when you ask, from the addresses in Settings: HVSC's own site, and the mirror its site links to, unless "
    "you change them. It does not contain HVSC's files and does not pass them on.";

struct PlayRecorder : spz::PlaySwitch {
    std::vector<std::string> log;
    void playing_off() override { log.push_back("off"); }
    void playing_on() override { log.push_back("on"); }
};

}  // namespace

TEST_CASE("SPEC-152 the notice's text is the specification's, word for word") {
    CHECK(spz::hvsc_notice_text() == kNotice);
}

TEST_CASE("SPEC-152 with the notice not accepted, asking opens the panel with that text and nothing goes on") {
    Record record;
    Panel panel;
    spz::NoticeGate gate(&record, &panel);
    int went_on = 0, cancelled = 0;

    CHECK_FALSE(gate.accepted());
    CHECK_FALSE(gate.asking());
    CHECK_FALSE(gate.ask([&] { ++went_on; }, [&] { ++cancelled; }));
    CHECK(gate.asking());
    REQUIRE(panel.opened.size() == 1);
    CHECK(panel.opened[0] == kNotice);
    CHECK(went_on == 0);
    CHECK(cancelled == 0);
    CHECK_FALSE(record.accepted);
}

TEST_CASE("SPEC-152 Enter accepts the notice, sets notice_accepted, and what was asked for goes on, once") {
    Record record;
    Panel panel;
    spz::NoticeGate gate(&record, &panel);
    int went_on = 0, cancelled = 0;
    gate.ask([&] { ++went_on; }, [&] { ++cancelled; });

    gate.accept();
    CHECK(record.accepted);
    CHECK(gate.accepted());
    CHECK_FALSE(gate.asking());
    CHECK(went_on == 1);
    CHECK(cancelled == 0);

    // From now on nothing is asked: what a key wants goes on at once, and no panel opens.
    CHECK(gate.ask([&] { ++went_on; }, [&] { ++cancelled; }));
    CHECK(went_on == 2);
    CHECK(panel.opened.size() == 1);
    CHECK_FALSE(gate.asking());

    // A second Enter with no panel open does nothing.
    gate.accept();
    CHECK(went_on == 2);
}

TEST_CASE("SPEC-152 ESC cancels what was asked for and leaves notice_accepted off, so the next key asks again") {
    Record record;
    Panel panel;
    spz::NoticeGate gate(&record, &panel);
    int went_on = 0, cancelled = 0;
    gate.ask([&] { ++went_on; }, [&] { ++cancelled; });

    gate.cancel();
    CHECK_FALSE(record.accepted);
    CHECK_FALSE(gate.accepted());
    CHECK_FALSE(gate.asking());
    CHECK(went_on == 0);
    CHECK(cancelled == 1);

    // Enter afterwards, with no panel open, accepts nothing.
    gate.accept();
    CHECK_FALSE(record.accepted);
    CHECK(went_on == 0);

    CHECK_FALSE(gate.ask([&] { ++went_on; }));
    CHECK(panel.opened.size() == 2);
    CHECK(went_on == 0);
}

TEST_CASE("SPEC-152 a notice accepted in an earlier run is accepted: nothing is asked") {
    Record record;
    record.accepted = true;
    Panel panel;
    spz::NoticeGate gate(&record, &panel);
    int went_on = 0;
    CHECK(gate.accepted());
    CHECK(gate.ask([&] { ++went_on; }));
    CHECK(went_on == 1);
    CHECK(panel.opened.empty());
}

TEST_CASE("SPEC-152 SPEC-140 with the notice not accepted, Get HVSC opens the panel and makes no request; ESC makes none; Enter makes the requests") {
    World w;
    serve_collection(&w.http, 85);
    spz::HvscSetup setup(setup_parts(w));

    setup.start();
    w.run();
    CHECK(w.panel.opened.size() == 1);
    CHECK(w.http.calls == 0);
    CHECK_FALSE(w.jobs.running());

    w.gate.cancel();
    w.run();
    CHECK_FALSE(w.record.accepted);
    CHECK(w.http.calls == 0);
    CHECK_FALSE(w.jobs.running());
    CHECK(w.notices.raised.empty());
    CHECK_FALSE(exists(w.root + "/DOCUMENTS/Songlengths.md5"));

    setup.start();
    CHECK(w.panel.opened.size() == 2);
    w.gate.accept();
    w.run();
    CHECK(w.record.accepted);
    CHECK(w.http.log.size() == 5);
    CHECK(w.notices.count("MSG_HVSC_READY") == 1);
}

TEST_CASE("SPEC-152 SPEC-141 with the notice not accepted, Enter on an absent tune opens the panel and makes no request; ESC makes none; Enter makes the request") {
    World w;
    w.put_documents(85);
    serve_collection(&w.http, 85);
    spz::Listing listing;
    REQUIRE(w.open_listing(&listing));
    Heard heard;
    spz::Fetcher fetcher(fetcher_parts(w, &listing));
    fetcher.set_listener(&heard);

    CHECK(fetcher.request(kCommando, spz::FetchWhy::Chosen) == spz::FetchAnswer::AskingNotice);
    w.run();
    CHECK(w.panel.opened.size() == 1);
    CHECK(w.http.calls == 0);
    CHECK(heard.events.empty());

    w.gate.cancel();
    w.run();
    CHECK_FALSE(w.record.accepted);
    CHECK(w.http.calls == 0);
    CHECK(heard.events == std::vector<std::string>{"cancelled " + kCommando});
    CHECK_FALSE(exists(w.in_root(kCommando)));

    CHECK(fetcher.request(kCommando, spz::FetchWhy::Chosen) == spz::FetchAnswer::AskingNotice);
    CHECK(w.panel.opened.size() == 2);
    w.gate.accept();
    w.run();
    CHECK(w.record.accepted);
    CHECK(urls(w.http) == std::vector<std::string>{kUrlFiles + kCommando});
    CHECK(heard.events == std::vector<std::string>{"cancelled " + kCommando, "started " + kCommando, "fetched " + kCommando});
    CHECK(exists(w.in_root(kCommando)));
}

TEST_CASE("SPEC-152 SPEC-143 with the notice not accepted, nothing that happens by itself opens the panel or makes a request") {
    World w;
    w.put_documents(85);
    serve_collection(&w.http, 85);
    spz::Listing listing;
    REQUIRE(w.open_listing(&listing));
    Heard heard;
    spz::Fetcher fetcher(fetcher_parts(w, &listing));
    fetcher.set_listener(&heard);

    // Unattended playback reaches an absent tune, and would fetch the one after it ahead.
    CHECK(fetcher.request(kCommando, spz::FetchWhy::Unattended) == spz::FetchAnswer::NoticeNeeded);
    CHECK(fetcher.request(kThreeSongs, spz::FetchWhy::Ahead) == spz::FetchAnswer::NoticeNeeded);
    w.clock.advance(120 * 1000);
    w.run();
    CHECK(w.panel.opened.empty());
    CHECK_FALSE(w.gate.asking());
    CHECK(w.http.calls == 0);
    CHECK(heard.events.empty());
    CHECK_FALSE(w.record.accepted);
}

#if SPZ_PART_RELEASES
TEST_CASE("SPEC-152 SPEC-148 with the notice not accepted, Check for a newer release opens the panel and makes no request; Enter makes it [parts: releases]") {
    World w;
    w.put_documents(84);
    serve_collection(&w.http, 85);
    spz::Listing listing;
    REQUIRE(w.open_listing(&listing));
    spz::ReleasesParts parts;
    parts.files = &w.files; parts.http = &w.http; parts.long_worker = &w.long_worker; parts.ui = &w.ui;
    parts.gate = &w.gate; parts.jobs = &w.jobs; parts.notices = &w.notices; parts.settings = &w.settings;
    parts.root = w.root; parts.cache = w.cache;
    spz::Releases releases(parts);

    releases.check();
    w.run();
    CHECK(w.panel.opened.size() == 1);
    CHECK(w.http.calls == 0);

    w.gate.cancel();
    w.run();
    CHECK_FALSE(w.record.accepted);
    CHECK(w.http.calls == 0);
    CHECK(w.notices.raised.empty());

    releases.check();
    w.gate.accept();
    w.run();
    CHECK(w.record.accepted);
    CHECK(urls(w.http) == std::vector<std::string>{kUrlFiles + "/DOCUMENTS/hv_sids.txt"});
}
#endif

#if SPZ_PART_DOWNLOAD_ALL
TEST_CASE("SPEC-152 SPEC-144 with the notice not accepted, Download everything opens the panel and makes no request; Enter makes it [parts: download-all]") {
    World w;
    w.put_documents(85);
    spz::Listing listing;
    REQUIRE(w.open_listing(&listing));
    spz::fake::HttpClient::Answer archive;
    archive.body = spz::test::bytes("an archive, as far as the fake unpacker cares");
    w.http.answer(archive_url(85), archive);
    w.files.set_free_bytes(4096 * kMiB);
    spz::fake::Unpacker unpacker;
    spz::fake::Memory memory;
    PlayRecorder play;
    spz::CompleteDownloadParts parts;
    parts.files = &w.files; parts.http = &w.http; parts.unpacker = &unpacker; parts.memory = &memory;
    parts.long_worker = &w.long_worker; parts.ui = &w.ui; parts.gate = &w.gate; parts.jobs = &w.jobs;
    parts.notices = &w.notices; parts.settings = &w.settings; parts.root = w.root; parts.cache = w.cache;
    parts.play = &play;
    spz::CompleteDownload download(parts);

    download.start();
    w.run();
    CHECK(w.panel.opened.size() == 1);
    CHECK(w.http.calls == 0);
    CHECK(unpacker.log.empty());
    CHECK(play.log.empty());                    // the player is not stopped for a download that was not begun
    CHECK_FALSE(w.jobs.running());

    w.gate.cancel();
    w.run();
    CHECK_FALSE(w.record.accepted);
    CHECK(w.http.calls == 0);
    CHECK(play.log.empty());
    CHECK(w.notices.raised.empty());

    download.start();
    w.gate.accept();
    w.run();
    CHECK(w.record.accepted);
    REQUIRE(w.http.log.size() == 1);
    CHECK(w.http.log[0].url == archive_url(85));
}
#endif
