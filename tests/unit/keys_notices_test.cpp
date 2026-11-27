// SPDX-License-Identifier: GPL-3.0-or-later
// The queue of notices (SPEC-113), on the fake clock: three seconds each, one at a time and in order, at
// most five waiting, and a notice with an identifier that is already there taking that one's place.
#include <string>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "spz/notices.h"

namespace {

// The identifier showing, or "" for none.
std::string showing(const spz::NoticeQueue& queue) {
    const spz::Notice* notice = queue.showing();
    return notice ? notice->id : "";
}

std::string text_showing(const spz::NoticeQueue& queue) {
    const spz::Notice* notice = queue.showing();
    return notice ? notice->text : "";
}

std::string ids(const std::vector<spz::Notice>& notices) {
    std::string all;
    for (const spz::Notice& notice : notices) all += (all.empty() ? "" : " ") + notice.id;
    return all;
}

}  // namespace

TEST_CASE("SPEC-113 a notice shows at once and for three seconds of the clock") {
    spz::fake::Clock clock;
    spz::NoticeQueue queue(&clock);
    CHECK(queue.showing() == nullptr);
    queue.raise("MSG_INDEX_READY", "The search index is ready");
    REQUIRE(queue.showing() != nullptr);
    CHECK(queue.showing()->id == "MSG_INDEX_READY");
    CHECK(queue.showing()->text == "The search index is ready");
    CHECK(queue.waiting().empty());
    clock.advance(2999);
    CHECK(showing(queue) == "MSG_INDEX_READY");
    clock.advance(1);
    CHECK(queue.showing() == nullptr);
    CHECK(spz::kNoticeMs == 3000);
    CHECK(clock.calls > 0);                              // its time is the Clock seam's
}

TEST_CASE("SPEC-113 three different notices raised at once show one after another, in the order raised, three seconds each") {
    spz::fake::Clock clock;
    spz::NoticeQueue queue(&clock);
    queue.raise("MSG_SETTINGS_RESET", "one");
    queue.raise("MSG_VOLUME", "two");
    queue.raise("MSG_VOLUME_FAILED", "three");
    CHECK(showing(queue) == "MSG_SETTINGS_RESET");
    CHECK(ids(queue.waiting()) == "MSG_VOLUME MSG_VOLUME_FAILED");
    clock.advance(2999);
    CHECK(showing(queue) == "MSG_SETTINGS_RESET");
    clock.advance(1);
    CHECK(showing(queue) == "MSG_VOLUME");
    CHECK(text_showing(queue) == "two");
    CHECK(ids(queue.waiting()) == "MSG_VOLUME_FAILED");
    clock.advance(2999);
    CHECK(showing(queue) == "MSG_VOLUME");
    clock.advance(1);
    CHECK(showing(queue) == "MSG_VOLUME_FAILED");
    CHECK(queue.waiting().empty());
    clock.advance(2999);
    CHECK(showing(queue) == "MSG_VOLUME_FAILED");
    clock.advance(1);
    CHECK(queue.showing() == nullptr);                   // nine seconds in all
    clock.advance(60000);
    CHECK(queue.showing() == nullptr);
}

TEST_CASE("SPEC-113 a notice with the identifier of the one showing takes its place at once and its time starts again") {
    spz::fake::Clock clock;
    spz::NoticeQueue queue(&clock);
    // Five quick presses of a volume key, within a second: one notice is left, with the last level.
    for (int level = 55; level <= 75; level += 5) {
        queue.raise("MSG_VOLUME", "Volume " + std::to_string(level) + " %");
        CHECK(text_showing(queue) == "Volume " + std::to_string(level) + " %");
        CHECK(queue.waiting().empty());
        clock.advance(200);
    }
    CHECK(text_showing(queue) == "Volume 75 %");
    // The last was raised at 800 ms and the clock is at 1,000: it shows until 3,800.
    clock.advance(2799);
    CHECK(text_showing(queue) == "Volume 75 %");
    clock.advance(1);
    CHECK(queue.showing() == nullptr);
    clock.advance(20000);
    CHECK(queue.showing() == nullptr);                   // none of the five was kept waiting
}

TEST_CASE("SPEC-113 a notice with the identifier of one that waits takes that one's place in the line") {
    spz::fake::Clock clock;
    spz::NoticeQueue queue(&clock);
    queue.raise("MSG_INDEX_READY", "ready");
    queue.raise("MSG_VOLUME", "Volume 55 %");
    queue.raise("MSG_FOLDER_UNREADABLE", "folder");
    queue.raise("MSG_VOLUME", "Volume 60 %");
    CHECK(showing(queue) == "MSG_INDEX_READY");
    const std::vector<spz::Notice> waiting = queue.waiting();
    REQUIRE(waiting.size() == 2);
    CHECK(waiting[0].id == "MSG_VOLUME");                // still ahead of the one raised after it
    CHECK(waiting[0].text == "Volume 60 %");
    CHECK(waiting[1].id == "MSG_FOLDER_UNREADABLE");
    clock.advance(3000);
    CHECK(text_showing(queue) == "Volume 60 %");
    clock.advance(3000);
    CHECK(showing(queue) == "MSG_FOLDER_UNREADABLE");
    clock.advance(3000);
    CHECK(queue.showing() == nullptr);
}

TEST_CASE("SPEC-113 at most five notices wait, and the one that has waited longest is dropped for a sixth") {
    spz::fake::Clock clock;
    spz::NoticeQueue queue(&clock);
    queue.raise("MSG_INDEX_READY", "showing");
    const std::vector<std::string> six = {"MSG_VOLUME", "MSG_VOLUME_FAILED", "MSG_FOLDER_UNREADABLE",
                                          "MSG_ROM_IGNORED", "MSG_SETTINGS_RESET", "MSG_INVALID_VALUE"};
    for (std::size_t i = 0; i < 5; ++i) queue.raise(six[i], "waiting");
    CHECK(queue.waiting().size() == 5);
    CHECK(ids(queue.waiting()) ==
          "MSG_VOLUME MSG_VOLUME_FAILED MSG_FOLDER_UNREADABLE MSG_ROM_IGNORED MSG_SETTINGS_RESET");
    queue.raise(six[5], "waiting");
    CHECK(spz::kNoticesWaiting == 5);
    CHECK(queue.waiting().size() == 5);
    CHECK(ids(queue.waiting()) ==
          "MSG_VOLUME_FAILED MSG_FOLDER_UNREADABLE MSG_ROM_IGNORED MSG_SETTINGS_RESET MSG_INVALID_VALUE");
    CHECK(showing(queue) == "MSG_INDEX_READY");          // the one showing is not one of the five
    std::string shown;
    for (int i = 0; i < 6; ++i) {
        shown += showing(queue) + " ";
        clock.advance(3000);
    }
    CHECK(shown == "MSG_INDEX_READY MSG_VOLUME_FAILED MSG_FOLDER_UNREADABLE MSG_ROM_IGNORED MSG_SETTINGS_RESET "
                   "MSG_INVALID_VALUE ");
    CHECK(queue.showing() == nullptr);
}

TEST_CASE("SPEC-113 MSG_TOO_HEAVY, which offers a key, shows for ten seconds, also after it has waited its turn") {
    spz::fake::Clock clock;
    spz::NoticeQueue queue(&clock);
    queue.raise("MSG_TOO_HEAVY", "Too heavy for the accurate engine. Press L for the light engine");
    clock.advance(9999);
    CHECK(showing(queue) == "MSG_TOO_HEAVY");
    clock.advance(1);
    CHECK(queue.showing() == nullptr);
    CHECK(spz::kNoticeWithKeyMs == 10000);

    queue.raise("MSG_VOLUME", "Volume 50 %");
    queue.raise("MSG_TOO_HEAVY", "Too heavy");
    queue.raise("MSG_INDEX_READY", "ready");
    clock.advance(3000);
    CHECK(showing(queue) == "MSG_TOO_HEAVY");
    clock.advance(9999);
    CHECK(showing(queue) == "MSG_TOO_HEAVY");
    clock.advance(1);
    CHECK(showing(queue) == "MSG_INDEX_READY");
    clock.advance(3000);
    CHECK(queue.showing() == nullptr);
}

TEST_CASE("SPEC-113 a notice raised when the last one has gone shows at once, for its own three seconds") {
    spz::fake::Clock clock;
    spz::NoticeQueue queue(&clock);
    queue.raise("MSG_VOLUME", "Volume 50 %");
    clock.advance(3000);
    REQUIRE(queue.showing() == nullptr);
    clock.advance(1234);
    queue.raise("MSG_INDEX_READY", "ready");
    CHECK(showing(queue) == "MSG_INDEX_READY");
    clock.advance(2999);
    CHECK(showing(queue) == "MSG_INDEX_READY");
    clock.advance(1);
    CHECK(queue.showing() == nullptr);
}

TEST_CASE("SPEC-106 SPEC-113 a notice can be taken away before its time, and the next in line then has its turn") {
    spz::fake::Clock clock;
    spz::NoticeQueue queue(&clock);
    queue.raise("MSG_HOLD_TO_LEAVE", "Keep holding ESC to leave");
    clock.advance(200);
    queue.remove("MSG_HOLD_TO_LEAVE");                   // ESC let go between 500 and 1,000 ms
    CHECK(queue.showing() == nullptr);
    clock.advance(10000);
    CHECK(queue.showing() == nullptr);

    queue.raise("MSG_HOLD_TO_LEAVE", "Keep holding ESC to leave");
    queue.raise("MSG_VOLUME", "Volume 50 %");
    queue.raise("MSG_INDEX_READY", "ready");
    clock.advance(1000);
    queue.remove("MSG_INDEX_READY");                     // one that waits
    CHECK(ids(queue.waiting()) == "MSG_VOLUME");
    queue.remove("MSG_HOLD_TO_LEAVE");                   // the one showing
    CHECK(showing(queue) == "MSG_VOLUME");
    clock.advance(2999);
    CHECK(showing(queue) == "MSG_VOLUME");               // its three seconds began when its turn came
    clock.advance(1);
    CHECK(queue.showing() == nullptr);
    queue.remove("MSG_VOLUME");                          // not there any more: nothing happens
    CHECK(queue.showing() == nullptr);
}

TEST_CASE("SPEC-113 whoever draws the notice is told each time the one showing changes") {
    spz::fake::Clock clock;
    spz::NoticeQueue queue(&clock);
    std::vector<std::string> seen;
    queue.on_change([&] { seen.push_back(text_showing(queue)); });
    queue.raise("MSG_VOLUME", "Volume 55 %");
    queue.raise("MSG_INDEX_READY", "ready");             // waits: what shows has not changed
    queue.raise("MSG_VOLUME", "Volume 60 %");            // takes the place of the one showing
    clock.advance(3000);
    clock.advance(3000);
    CHECK(seen == std::vector<std::string>{"Volume 55 %", "Volume 60 %", "ready", ""});
}

TEST_CASE("SPEC-113 FIND-025 the answer to a key shows at once in place of an earlier key's answer, which is done with") {
    spz::fake::Clock clock;
    spz::NoticeQueue queue(&clock);
    queue.answer("MSG_FAV_ADDED", "Added to Favourites");
    clock.advance(500);
    queue.answer("MSG_VOLUME", "Muted");
    CHECK(showing(queue) == "MSG_VOLUME");
    CHECK(text_showing(queue) == "Muted");
    CHECK(queue.waiting().empty());                      // the earlier answer does not come back
    clock.advance(2999);
    CHECK(showing(queue) == "MSG_VOLUME");               // its own three seconds, from when it showed
    clock.advance(1);
    CHECK(queue.showing() == nullptr);
}

TEST_CASE("SPEC-113 FIND-025 the answer to a key goes before a notice from the background, which has its turn after it") {
    spz::fake::Clock clock;
    spz::NoticeQueue queue(&clock);
    queue.raise("MSG_INDEX_READY", "The search index is ready");
    queue.raise("MSG_HVSC_READY", "HVSC is set up");
    clock.advance(1000);
    queue.answer("MSG_VOLUME", "Volume 45 %");
    CHECK(showing(queue) == "MSG_VOLUME");
    CHECK(ids(queue.waiting()) == "MSG_INDEX_READY MSG_HVSC_READY");
    clock.advance(3000);
    CHECK(showing(queue) == "MSG_INDEX_READY");          // the background notice has its turn again, whole
    clock.advance(3000);
    CHECK(showing(queue) == "MSG_HVSC_READY");
}

TEST_CASE("SPEC-113 FIND-025 a notice from the background raised while an answer shows waits its turn behind it") {
    spz::fake::Clock clock;
    spz::NoticeQueue queue(&clock);
    queue.answer("MSG_VOLUME", "Volume 45 %");
    queue.raise("MSG_INDEX_READY", "The search index is ready");
    CHECK(showing(queue) == "MSG_VOLUME");
    CHECK(ids(queue.waiting()) == "MSG_INDEX_READY");
    queue.answer("MSG_VOLUME", "Volume 50 %");           // the same key again: its new answer, at once
    CHECK(text_showing(queue) == "Volume 50 %");
    CHECK(ids(queue.waiting()) == "MSG_INDEX_READY");
}
