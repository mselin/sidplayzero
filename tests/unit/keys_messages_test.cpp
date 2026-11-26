// SPDX-License-Identifier: GPL-3.0-or-later
// The message table (SPEC-116): the table in `core` against a copy of the specification's rows held here.
#include <map>
#include <set>
#include <string>
#include <vector>

#include "doctest.h"
#include "spz/messages.h"

namespace {

using spz::MessageKind;

struct Row {
    const char* id;
    MessageKind kind;
    const char* words;       // the words, when the table gives them whole; else nullptr
};

// SPEC-116's table, row for row. A row whose words hold something that changes (a number, a path, a name,
// a step) has nullptr here and a test of its own below.
const std::vector<Row> kTable = {
    {"MSG_INVALID_SID", MessageKind::State, "Not a valid SID file"},
    {"MSG_ENGINE_REFUSED", MessageKind::State, "This tune cannot be played"},
    {"MSG_NEEDS_BASIC", MessageKind::State, "This tune needs the C64 BASIC ROM"},
    {"MSG_FETCH_NOT_FOUND", MessageKind::State, "Not on HVSC's site. Check for a newer HVSC release"},
    {"MSG_FETCH_MISMATCH", MessageKind::State,
     "The download does not match HVSC's record. Check for a newer HVSC release"},
    {"MSG_FETCH_NETWORK", MessageKind::State, "Cannot fetch: no network"},
    {"MSG_FETCH_TOO_LARGE", MessageKind::State, "Cannot fetch: the file is too large"},
    {"MSG_CARD_FULL", MessageKind::State, "The card is full"},
    {"MSG_CANNOT_WRITE", MessageKind::State, "The collection folder cannot be written"},
    {"MSG_NO_SOUND", MessageKind::State, "No sound device"},
    {"MSG_USBSID_MISSING", MessageKind::State, "USBSID-Pico is not connected"},
    {"MSG_ENTRY_MISSING", MessageKind::State, "This tune is not on the device and not in HVSC"},
    {"MSG_NOTICE_NEEDED", MessageKind::Notice,
     "Not fetched: play a tune that is not on the device to read HVSC's notice first"},
    {"MSG_STOPPED_BAD_FILES", MessageKind::Notice, "Stopped: several files in a row could not be played"},
    {"MSG_REST_ABSENT", MessageKind::Notice, "The rest of this list is not on the device"},
    {"MSG_FOLDER_UNREADABLE", MessageKind::Notice, "This folder cannot be read"},
    {"MSG_ROM_IGNORED", MessageKind::Notice, "A ROM file was ignored: not found, or the wrong size"},
    {"MSG_SETTINGS_RESET", MessageKind::Notice, "Some settings could not be read and were set to their defaults"},
    {"MSG_LENGTHS_UNREADABLE", MessageKind::Notice, "HVSC's song lengths could not be read"},
    {"MSG_VOLUME", MessageKind::Notice, nullptr},
    {"MSG_VOLUME_FAILED", MessageKind::Notice, "The volume could not be changed"},
    {"MSG_BRIGHTNESS", MessageKind::Notice, nullptr},
    {"MSG_BRIGHTNESS_UNAVAILABLE", MessageKind::Notice, "The brightness cannot be changed on this device"},
    {"MSG_MODE", MessageKind::Notice, nullptr},
    {"MSG_SWITCHED_LIGHT", MessageKind::Notice, "Switched to the light engine for this tune"},
    {"MSG_TOO_HEAVY", MessageKind::Notice, "Too heavy for the accurate engine. Press L for the light engine"},
    {"MSG_CANNOT_KEEP_UP", MessageKind::Notice, "This tune is too heavy for this device"},
    {"MSG_HOLD_TO_LEAVE", MessageKind::Notice, "Keep holding ESC to leave"},
    {"MSG_SCREENSHOT_SAVED", MessageKind::Notice, nullptr},
    {"MSG_SCREENSHOT_FAILED", MessageKind::Notice, "The screenshot could not be saved"},
    {"MSG_FAV_ADDED", MessageKind::Notice, "Added to Favourites"},
    {"MSG_FAV_REMOVED", MessageKind::Notice, "Removed from Favourites"},
    {"MSG_ADDED", MessageKind::Notice, nullptr},
    {"MSG_ALREADY_THERE", MessageKind::Notice, "Already in that playlist"},
    {"MSG_NAME_TAKEN", MessageKind::Notice, "There is already a playlist with that name"},
    {"MSG_INVALID_VALUE", MessageKind::Notice, "That value is not allowed"},
    {"MSG_FILE_TOO_LARGE", MessageKind::Notice, "That file is too large to read"},
    {"MSG_LIST_CUT", MessageKind::Notice, "Only the first 20,000 entries were read"},
    {"MSG_EXPORTED", MessageKind::Notice, nullptr},
    {"MSG_FETCHED_SOME", MessageKind::Notice, nullptr},
    {"MSG_HVSC_READY", MessageKind::Notice, nullptr},
    {"MSG_HVSC_FAILED", MessageKind::Notice, nullptr},
    {"MSG_HVSC_NEWEST", MessageKind::Notice, nullptr},
    {"MSG_HVSC_NEWER", MessageKind::Notice, nullptr},
    {"MSG_DOWNLOAD_DONE", MessageKind::Notice, "All of HVSC is on the device"},
    {"MSG_DOWNLOAD_FAILED", MessageKind::Notice, nullptr},
    {"MSG_UNPACKING", MessageKind::Notice, "HVSC is being unpacked. Playing is off until it is done"},
    {"MSG_NEED_ROOM", MessageKind::Notice, nullptr},
    {"MSG_NEED_7ZIP", MessageKind::Text, nullptr},        // SPEC-145 has the words of these three
    {"MSG_NEED_MEMORY", MessageKind::Text, nullptr},
    {"MSG_NEED_WRITE", MessageKind::Text, nullptr},
    {"MSG_CHECK_FAILED", MessageKind::Notice, "Could not check for a newer release"},
    {"MSG_IMPORT_NOTHING", MessageKind::Text, "Nothing in that file could be read as a playlist"},
    {"MSG_INDEX_READY", MessageKind::Notice, "The search index is ready"},
    {"MSG_SELFTEST", MessageKind::Notice, "Self-test running: leave the device alone"},
};

bool starts_with(const std::string& text, const std::string& start) { return text.compare(0, start.size(), start) == 0; }

}  // namespace

TEST_CASE("SPEC-116 the table in core has the specification's rows, each identifier once, with its kind, and no other") {
    const std::vector<spz::MessageRow>& table = spz::message_table();
    REQUIRE(kTable.size() == 55);
    REQUIRE(table.size() == kTable.size());
    std::set<std::string> ids;
    for (std::size_t i = 0; i < kTable.size(); ++i) {
        CAPTURE(kTable[i].id);
        CHECK(std::string(table[i].id) == kTable[i].id);
        CHECK(table[i].kind == kTable[i].kind);
        CHECK(ids.insert(table[i].id).second);           // no two rows share an identifier
        CHECK(spz::is_message(kTable[i].id));
    }
    CHECK_FALSE(spz::is_message("MSG_MADE_UP"));
    CHECK_FALSE(spz::is_message(""));
    CHECK_FALSE(spz::is_message("msg_volume"));
}

TEST_CASE("SPEC-116 each message whose words the table gives whole has exactly those words") {
    int whole = 0;
    for (const Row& row : kTable) {
        if (row.words == nullptr) continue;
        ++whole;
        CAPTURE(row.id);
        CHECK(spz::message_words(row.id) == row.words);
    }
    CHECK(whole == 39);
}

TEST_CASE("SPEC-116 a message about a number has the number in the table's place for n or m") {
    CHECK(spz::message_words("MSG_VOLUME", {{"n", "45"}}) == "Volume 45 %");
    CHECK(spz::message_words("MSG_VOLUME", {{"n", "100"}}) == "Volume 100 %");
    CHECK(spz::message_words("MSG_BRIGHTNESS", {{"n", "60"}}) == "Brightness 60 %");
    CHECK(spz::message_words("MSG_HVSC_READY", {{"n", "85"}}) == "HVSC #85 is ready");
    CHECK(spz::message_words("MSG_HVSC_NEWEST", {{"n", "85"}}) == "HVSC #85 is the newest release");
    CHECK(spz::message_words("MSG_HVSC_NEWER", {{"m", "86"}}) == "HVSC #86 is available");
    CHECK(spz::message_words("MSG_NEED_ROOM", {{"n", "120"}, {"m", "80"}}) ==
          "Not enough room: 120 MiB needed, 80 MiB free");
    const std::string fetched = spz::message_words("MSG_FETCHED_SOME", {{"n", "200"}, {"m", "50"}});
    CHECK(starts_with(fetched, "Fetched 200."));
    CHECK(fetched.find("50 remain") != std::string::npos);
}

TEST_CASE("SPEC-116 a message about a path, a name, a step or a value begins with the table's words") {
    const std::map<std::string, std::string> begins = {
        {"MSG_MODE", "When a song ends:"},
        {"MSG_SCREENSHOT_SAVED", "Saved"},
        {"MSG_ADDED", "Added to"},
        {"MSG_EXPORTED", "Exported to"},
        {"MSG_HVSC_FAILED", "HVSC could not be set up:"},
        {"MSG_DOWNLOAD_FAILED", "The download failed:"},
    };
    for (const auto& entry : begins) {
        CAPTURE(entry.first);
        CHECK(starts_with(spz::message_words(entry.first), entry.second));
    }
    // SPEC-108: the notice for a screenshot shows the path.
    const std::string saved =
        spz::message_words("MSG_SCREENSHOT_SAVED", {{"path", "~/Pictures/Screenshots/sidplayzero_20261125_000000_000.png"}});
    CHECK(starts_with(saved, "Saved"));
    CHECK(saved.find("~/Pictures/Screenshots/sidplayzero_20261125_000000_000.png") != std::string::npos);
}
