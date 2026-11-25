// SPDX-License-Identifier: GPL-3.0-or-later
// The browser's model: SPEC-043 (folders are listed off the UI thread, sixteen are kept in memory, a row
// is given from memory), SPEC-044 (opening, going back, where the highlight lands), SPEC-045 (moving in
// the list and the position in the top line). The keys, their repeat and the drawing are the simulator's
// tests.
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "browse_support.h"
#include "doctest.h"
#include "fakes.h"
#include "spz/browser_model.h"
#include "spz/collection.h"
#include "support.h"

using spz::test::TestRoot;
using spz::test::listings_of;
using spz::test::move_time_on;
using spz::test::run_workers;

namespace {

const char* const kCommando = "tunes/commando_test.sid";

// "/BIG/Tune_0001.sid" for 1.
std::string numbered(const char* form, int number) {
    char text[64];
    std::snprintf(text, sizeof text, form, number);
    return text;
}

// A folder of 1,298 tunes, as the largest folders of HVSC are (SPEC-043, SPEC-045). The lister does not
// open them, so each is one byte.
void make_big_folder(TestRoot* t) {
    for (int number = 1; number <= 1298; ++number) t->put_text(numbered("/BIG/Tune_%04d.sid", number), "x");
}

// What the screen would be told.
struct Seen : spz::BrowserListener {
    int changes = 0;
    std::vector<std::string> notices;
    void browser_changed() override { ++changes; }
    void browser_notice(const std::string& id) override { notices.push_back(id); }
};

}  // namespace

TEST_CASE("SPEC-043 a folder of 1,298 entries is listed, and the model gives any row from memory, in constant time") {
    TestRoot t;
    make_big_folder(&t);
    spz::Collection collection(&t.files, t.root, nullptr);
    spz::fake::Executor quick, ui;
    spz::BrowserModel model(&collection, &quick, &ui, &t.clock);

    model.show("/BIG");
    run_workers(quick, ui);
    REQUIRE(model.count() == 1298);
    CHECK(model.note() == spz::ListNote::None);
    CHECK(model.position() == "1/1298");

    // Any row, in any order, as often as asked: no file is touched and no worker is asked, so a row costs
    // the same whether the folder has six entries or 1,298.
    const int file_calls = t.files.calls, quick_calls = quick.calls, ui_calls = ui.calls;
    for (int index : {0, 1297, 648, 1, 1296, 649, 0, 1297}) {
        CAPTURE(index);
        CHECK(model.row(index).name == numbered("Tune_%04d.sid", index + 1));
    }
    bool every_row_right = true;
    for (int round = 0; round < 20; ++round) {
        for (int index = 1297; index >= 0; --index) {
            every_row_right = every_row_right && model.row(index).name == numbered("Tune_%04d.sid", index + 1) &&
                              model.row(index).present && !model.row(index).is_folder;
        }
    }
    CHECK(every_row_right);
    CHECK(t.files.calls == file_calls);
    CHECK(quick.calls == quick_calls);
    CHECK(ui.calls == ui_calls);
}

TEST_CASE("SPEC-043 opening a folder asks the quick worker for its entries; if they take more than 150 ms the list says Reading meanwhile, and if they come sooner it never does") {
    TestRoot t;
    t.put(kCommando, "/A/One.sid");
    t.put(kCommando, "/B/Two.sid");
    spz::Collection collection(&t.files, t.root, nullptr);
    spz::fake::Executor quick, ui;
    spz::BrowserModel model(&collection, &quick, &ui, &t.clock);

    model.show("/A");
    CHECK(quick.waiting() >= 1);                          // the work is the quick worker's,
    CHECK(listings_of(t.files, t.at("/A")) == 0);         // and none of it was done on the UI thread
    t.clock.advance(149);
    CHECK(model.note() != spz::ListNote::Reading);
    t.clock.advance(2);
    CHECK(model.note() == spz::ListNote::Reading);

    run_workers(quick, ui);
    CHECK(model.note() == spz::ListNote::None);
    REQUIRE(model.count() == 1);
    CHECK(model.row(0).name == "One.sid");
    CHECK(model.folder() == "/A");
    CHECK(listings_of(t.files, t.at("/A")) == 1);

    model.show("/B");
    run_workers(quick, ui);                               // these come at once
    CHECK(model.note() == spz::ListNote::None);
    t.clock.advance(1000);
    CHECK(model.note() == spz::ListNote::None);
    REQUIRE(model.count() == 1);
    CHECK(model.row(0).name == "Two.sid");
}

TEST_CASE("SPEC-043 an empty folder shows (empty) and the position 0/0") {
    TestRoot t;
    t.make_folder("/Nothing");
    t.put(kCommando, "/Something/One.sid");
    spz::Collection collection(&t.files, t.root, nullptr);
    spz::fake::Executor quick, ui;
    spz::BrowserModel model(&collection, &quick, &ui, &t.clock);

    model.show("/Nothing");
    run_workers(quick, ui);
    CHECK(model.count() == 0);
    CHECK(model.note() == spz::ListNote::Empty);
    CHECK(model.position() == "0/0");
    CHECK(model.highlight() == -1);

    model.show("/Something");
    run_workers(quick, ui);
    CHECK(model.note() == spz::ListNote::None);
    CHECK(model.position() == "1/1");
}

TEST_CASE("SPEC-043 a file added to a folder that is kept in memory appears when the folder is opened again") {
    TestRoot t;
    t.put(kCommando, "/A/One.sid");
    t.put(kCommando, "/B/Other.sid");
    spz::Collection collection(&t.files, t.root, nullptr);
    spz::fake::Executor quick, ui;
    spz::BrowserModel model(&collection, &quick, &ui, &t.clock);

    model.show("/A");
    run_workers(quick, ui);
    REQUIRE(model.count() == 1);
    model.show("/B");
    run_workers(quick, ui);

    t.put("tunes/two_sid.sid", "/A/Two.sid");
    move_time_on(t.at("/A"), 5);                          // the folder's time has changed
    model.show("/A");
    run_workers(quick, ui);
    REQUIRE(model.count() == 2);
    CHECK(model.row(0).name == "One.sid");
    CHECK(model.row(1).name == "Two.sid");
    CHECK(model.position() == "1/2");
    CHECK(listings_of(t.files, t.at("/A")) == 2);
}

TEST_CASE("SPEC-043 the last 16 folders listed stay in memory: opening one of them again does not list it again, and a seventeenth pushes the oldest out") {
    TestRoot t;
    for (int n = 1; n <= 17; ++n) t.put(kCommando, numbered("/F%02d", n) + numbered("/Tune_%02d.sid", n));
    spz::Collection collection(&t.files, t.root, nullptr);
    spz::fake::Executor quick, ui;
    spz::BrowserModel model(&collection, &quick, &ui, &t.clock);
    auto show = [&](int n) {
        model.show(numbered("/F%02d", n));
        run_workers(quick, ui);
        REQUIRE(model.count() == 1);
        CHECK(model.row(0).name == numbered("Tune_%02d.sid", n));
    };
    auto listed = [&](int n) { return listings_of(t.files, t.at(numbered("/F%02d", n))); };

    for (int n = 1; n <= 16; ++n) show(n);
    for (int n = 1; n <= 16; ++n) show(n);                // all sixteen again: each is still in memory
    for (int n = 1; n <= 16; ++n) {
        CAPTURE(n);
        CHECK(listed(n) == 1);
    }

    show(17);                                             // the seventeenth: the oldest, F01, makes room
    for (int n = 2; n <= 17; ++n) show(n);
    for (int n = 2; n <= 17; ++n) {
        CAPTURE(n);
        CHECK(listed(n) == 1);
    }
    show(1);
    CHECK(listed(1) == 2);
}

TEST_CASE("SPEC-043 a folder that has gained a present tune through the app is listed again when it is opened") {
    TestRoot t("collections/85/C64Music");
    const std::string folder = "/MUSICIANS/T/Tester_Tom";
    t.remove(folder + "/New_Chip.sid");
    spz::Collection collection(&t.files, t.root, t.open_listing());
    spz::fake::Executor quick, ui;
    spz::BrowserModel model(&collection, &quick, &ui, &t.clock);

    model.show(folder);
    run_workers(quick, ui);
    REQUIRE(model.count() == 7);
    REQUIRE(model.row(2).name == "New_Chip.sid");
    CHECK_FALSE(model.row(2).present);

    // The app fetches the tune (SPEC-141). The folder's time is put back to what it was, so that only the
    // app's own word says that the folder has changed.
    const auto time = std::filesystem::last_write_time(t.at(folder));
    t.put("tunes/model_8580.sid", folder + "/New_Chip.sid");
    std::filesystem::last_write_time(t.at(folder), time);
    model.tune_changed(folder + "/New_Chip.sid");

    model.show("/MUSICIANS/T");
    run_workers(quick, ui);
    model.show(folder);
    run_workers(quick, ui);
    REQUIRE(model.count() == 7);
    CHECK(model.row(2).name == "New_Chip.sid");
    CHECK(model.row(2).present);
}

TEST_CASE("SPEC-043 SPEC-041 a folder that cannot be read arrives with no rows, and its notice is passed on once") {
    TestRoot t;
    t.put(kCommando, "/Locked/Tune.sid");
    spz::test::FilesWithLockedFolder files;
    files.locked = t.at("/Locked");
    spz::Collection collection(&files, t.root, nullptr);
    spz::fake::Executor quick, ui;
    spz::BrowserModel model(&collection, &quick, &ui, &t.clock);
    Seen seen;
    model.set_listener(&seen);

    model.show("/Locked");
    run_workers(quick, ui);
    CHECK(model.count() == 0);
    CHECK(seen.notices == std::vector<std::string>{"MSG_FOLDER_UNREADABLE"});
    CHECK(seen.changes >= 1);
}

TEST_CASE("SPEC-044 a folder opens at its first entry, or at the entry highlighted the last time it was open; its parent opens with the folder just left highlighted; at the root nothing happens") {
    TestRoot t("collections/85/C64Music");
    spz::Collection collection(&t.files, t.root, t.open_listing());
    spz::fake::Executor quick, ui;
    spz::BrowserModel model(&collection, &quick, &ui, &t.clock);

    model.show("/");
    run_workers(quick, ui);
    REQUIRE(model.count() == 3);                          // DEMOS, GAMES, MUSICIANS
    CHECK(model.highlight() == 0);
    CHECK_FALSE(model.parent());                          // at the root: nothing
    run_workers(quick, ui);
    CHECK(model.folder() == "/");
    CHECK(model.highlight() == 0);

    model.down();
    model.down();
    REQUIRE(model.row(model.highlight()).name == "MUSICIANS");
    CHECK(model.open() == spz::BrowserModel::Opened::Folder);
    run_workers(quick, ui);
    CHECK(model.folder() == "/MUSICIANS");
    CHECK(model.highlight() == 0);                        // H; then T
    model.down();
    CHECK(model.open() == spz::BrowserModel::Opened::Folder);
    run_workers(quick, ui);
    CHECK(model.folder() == "/MUSICIANS/T");
    CHECK(model.open() == spz::BrowserModel::Opened::Folder);
    run_workers(quick, ui);
    REQUIRE(model.folder() == "/MUSICIANS/T/Tester_Tom");
    CHECK(model.highlight() == 0);

    // Down two rows, back, and in again: the third row is highlighted.
    model.down();
    model.down();
    CHECK(model.highlight() == 2);
    CHECK(model.parent());
    run_workers(quick, ui);
    CHECK(model.folder() == "/MUSICIANS/T");
    REQUIRE(model.highlight() == 0);
    CHECK(model.row(0).name == "Tester_Tom");             // the folder just left
    CHECK(model.open() == spz::BrowserModel::Opened::Folder);
    run_workers(quick, ui);
    CHECK(model.folder() == "/MUSICIANS/T/Tester_Tom");
    CHECK(model.highlight() == 2);
    CHECK(model.row(2).name == "New_Chip.sid");

    // Up the tree: each time the folder just left is the one highlighted.
    CHECK(model.parent());
    run_workers(quick, ui);
    CHECK(model.parent());
    run_workers(quick, ui);
    CHECK(model.folder() == "/MUSICIANS");
    REQUIRE(model.highlight() == 1);
    CHECK(model.row(1).name == "T");
    CHECK(model.parent());
    run_workers(quick, ui);
    CHECK(model.folder() == "/");
    REQUIRE(model.highlight() == 2);
    CHECK(model.row(2).name == "MUSICIANS");
}

TEST_CASE("SPEC-044 SPEC-095 Enter on a tune leaves the browser where it is and hands the tune to the caller; a folder can be shown with one of its entries highlighted") {
    TestRoot t("collections/85/C64Music");
    spz::Collection collection(&t.files, t.root, t.open_listing());
    spz::fake::Executor quick, ui;
    spz::BrowserModel model(&collection, &quick, &ui, &t.clock);

    // As "Folder" in Search and the start of the app do (SPEC-100, SPEC-131): a folder, and the entry to highlight.
    model.show("/MUSICIANS/T/Tester_Tom", "Three_Songs.sid");
    run_workers(quick, ui);
    REQUIRE(model.count() == 7);
    CHECK(model.highlight() == 5);
    CHECK(model.position() == "6/7");
    CHECK(model.row(5).name == "Three_Songs.sid");

    CHECK(model.open() == spz::BrowserModel::Opened::Tune);
    run_workers(quick, ui);
    CHECK(model.folder() == "/MUSICIANS/T/Tester_Tom");
    CHECK(model.highlight() == 5);

    // An entry that is not there: the first one.
    model.show("/MUSICIANS/H", "No_Such_Folder");
    run_workers(quick, ui);
    CHECK(model.highlight() == 0);
}

TEST_CASE("SPEC-045 up and down move one row and stop at the ends, home and end go to the first and last entry, a page is the rows on the screen, and the position reads n/total") {
    TestRoot t;
    make_big_folder(&t);
    spz::Collection collection(&t.files, t.root, nullptr);
    spz::fake::Executor quick, ui;
    spz::BrowserModel model(&collection, &quick, &ui, &t.clock);
    model.set_rows_on_screen(6);
    model.show("/BIG");
    run_workers(quick, ui);
    REQUIRE(model.count() == 1298);

    CHECK(model.position() == "1/1298");
    model.up();                                           // already at the first: stays
    CHECK(model.position() == "1/1298");
    model.down();
    CHECK(model.position() == "2/1298");
    CHECK(model.highlight() == 1);
    model.down();
    model.up();
    CHECK(model.position() == "2/1298");

    model.last();
    CHECK(model.position() == "1298/1298");
    CHECK(model.row(model.highlight()).name == "Tune_1298.sid");
    model.down();                                         // already at the last: stays
    CHECK(model.position() == "1298/1298");
    model.up();
    CHECK(model.position() == "1297/1298");

    model.first();
    CHECK(model.position() == "1/1298");
    model.page_down();                                    // six rows on the screen
    CHECK(model.position() == "7/1298");
    model.page_down();
    CHECK(model.position() == "13/1298");
    model.page_up();
    CHECK(model.position() == "7/1298");

    model.set_rows_on_screen(5);                          // five, when the detail line has the sixth (SPEC-091)
    model.page_down();
    CHECK(model.position() == "12/1298");
    model.page_up();
    model.page_up();
    CHECK(model.position() == "2/1298");
}
