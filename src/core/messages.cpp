// SPDX-License-Identifier: GPL-3.0-or-later
// The table of SPEC-116: the identifiers and kinds (in the form tests/framework/test_messages.py reads),
// then the words.
#include "spz/messages.h"

namespace spz {

const std::vector<MessageRow>& message_table() {
    static const std::vector<MessageRow> table = {
    {"MSG_INVALID_SID", MessageKind::State},
    {"MSG_ENGINE_REFUSED", MessageKind::State},
    {"MSG_NEEDS_BASIC", MessageKind::State},
    {"MSG_FETCH_NOT_FOUND", MessageKind::State},
    {"MSG_FETCH_MISMATCH", MessageKind::State},
    {"MSG_FETCH_NETWORK", MessageKind::State},
    {"MSG_FETCH_TOO_LARGE", MessageKind::State},
    {"MSG_CARD_FULL", MessageKind::State},
    {"MSG_CANNOT_WRITE", MessageKind::State},
    {"MSG_NO_SOUND", MessageKind::State},
    {"MSG_USBSID_MISSING", MessageKind::State},
    {"MSG_ENTRY_MISSING", MessageKind::State},
    {"MSG_NOTICE_NEEDED", MessageKind::Notice},
    {"MSG_STOPPED_BAD_FILES", MessageKind::Notice},
    {"MSG_REST_ABSENT", MessageKind::Notice},
    {"MSG_FOLDER_UNREADABLE", MessageKind::Notice},
    {"MSG_ROM_IGNORED", MessageKind::Notice},
    {"MSG_SETTINGS_RESET", MessageKind::Notice},
    {"MSG_LENGTHS_UNREADABLE", MessageKind::Notice},
    {"MSG_VOLUME", MessageKind::Notice},
    {"MSG_VOLUME_FAILED", MessageKind::Notice},
    {"MSG_BRIGHTNESS", MessageKind::Notice},
    {"MSG_BRIGHTNESS_UNAVAILABLE", MessageKind::Notice},
    {"MSG_MODE", MessageKind::Notice},
    {"MSG_SWITCHED_LIGHT", MessageKind::Notice},
    {"MSG_TOO_HEAVY", MessageKind::Notice},
    {"MSG_CANNOT_KEEP_UP", MessageKind::Notice},
    {"MSG_HOLD_TO_LEAVE", MessageKind::Notice},
    {"MSG_SCREENSHOT_SAVED", MessageKind::Notice},
    {"MSG_SCREENSHOT_FAILED", MessageKind::Notice},
    {"MSG_FAV_ADDED", MessageKind::Notice},
    {"MSG_FAV_REMOVED", MessageKind::Notice},
    {"MSG_ADDED", MessageKind::Notice},
    {"MSG_ALREADY_THERE", MessageKind::Notice},
    {"MSG_NAME_TAKEN", MessageKind::Notice},
    {"MSG_INVALID_VALUE", MessageKind::Notice},
    {"MSG_FILE_TOO_LARGE", MessageKind::Notice},
    {"MSG_LIST_CUT", MessageKind::Notice},
    {"MSG_EXPORTED", MessageKind::Notice},
    {"MSG_FETCHED_SOME", MessageKind::Notice},
    {"MSG_HVSC_READY", MessageKind::Notice},
    {"MSG_HVSC_FAILED", MessageKind::Notice},
    {"MSG_HVSC_NEWEST", MessageKind::Notice},
    {"MSG_HVSC_NEWER", MessageKind::Notice},
    {"MSG_DOWNLOAD_DONE", MessageKind::Notice},
    {"MSG_DOWNLOAD_FAILED", MessageKind::Notice},
    {"MSG_UNPACKING", MessageKind::Notice},
    {"MSG_NEED_ROOM", MessageKind::Notice},
    {"MSG_NEED_7ZIP", MessageKind::Text},
    {"MSG_NEED_MEMORY", MessageKind::Text},
    {"MSG_NEED_WRITE", MessageKind::Text},
    {"MSG_CHECK_FAILED", MessageKind::Notice},
    {"MSG_IMPORT_NOTHING", MessageKind::Text},
    {"MSG_INDEX_READY", MessageKind::Notice},
    {"MSG_SELFTEST", MessageKind::Notice},
    };
    return table;
}

bool is_message(const std::string& id) {
    for (const MessageRow& row : message_table()) {
        if (id == row.id) return true;
    }
    return false;
}

namespace {

// The words of each message. "{x}" is where `about`'s x goes (FIND-024: n, m, path, name, step).
const std::map<std::string, std::string>& words_table() {
    static const std::map<std::string, std::string> words = {
        {"MSG_INVALID_SID", "Not a valid SID file"},
        {"MSG_ENGINE_REFUSED", "This tune cannot be played"},
        {"MSG_NEEDS_BASIC", "This tune needs the C64 BASIC ROM"},
        {"MSG_FETCH_NOT_FOUND", "Not on HVSC's site. Check for a newer HVSC release"},
        {"MSG_FETCH_MISMATCH", "The download does not match HVSC's record. Check for a newer HVSC release"},
        {"MSG_FETCH_NETWORK", "Cannot fetch: no network"},
        {"MSG_FETCH_TOO_LARGE", "Cannot fetch: the file is too large"},
        {"MSG_CARD_FULL", "The card is full"},
        {"MSG_CANNOT_WRITE", "The collection folder cannot be written"},
        {"MSG_NO_SOUND", "No sound device"},
        {"MSG_USBSID_MISSING", "USBSID-Pico is not connected"},
        {"MSG_ENTRY_MISSING", "This tune is not on the device and not in HVSC"},
        {"MSG_NOTICE_NEEDED", "Not fetched: play a tune that is not on the device to read HVSC's notice first"},
        {"MSG_STOPPED_BAD_FILES", "Stopped: several files in a row could not be played"},
        {"MSG_REST_ABSENT", "The rest of this list is not on the device"},
        {"MSG_FOLDER_UNREADABLE", "This folder cannot be read"},
        {"MSG_ROM_IGNORED", "A ROM file was ignored: not found, or the wrong size"},
        {"MSG_SETTINGS_RESET", "Some settings could not be read and were set to their defaults"},
        {"MSG_LENGTHS_UNREADABLE", "HVSC's song lengths could not be read"},
        {"MSG_VOLUME", "Volume {n} %"},                  // "Muted" when not given n
        {"MSG_VOLUME_FAILED", "The volume could not be changed"},
        {"MSG_BRIGHTNESS", "Brightness {n} %"},
        {"MSG_BRIGHTNESS_UNAVAILABLE", "The brightness cannot be changed on this device"},
        {"MSG_MODE", "When a song ends: {name}"},
        {"MSG_SWITCHED_LIGHT", "Switched to the light engine for this tune"},
        {"MSG_TOO_HEAVY", "Too heavy for the accurate engine. Press L for the light engine"},
        {"MSG_CANNOT_KEEP_UP", "This tune is too heavy for this device"},
        {"MSG_HOLD_TO_LEAVE", "Keep holding ESC to leave"},
        {"MSG_SCREENSHOT_SAVED", "Saved {path}"},
        {"MSG_SCREENSHOT_FAILED", "The screenshot could not be saved"},
        {"MSG_FAV_ADDED", "Added to Favourites"},
        {"MSG_FAV_REMOVED", "Removed from Favourites"},
        {"MSG_ADDED", "Added to {name}"},
        {"MSG_ALREADY_THERE", "Already in that playlist"},
        {"MSG_NAME_TAKEN", "There is already a playlist with that name"},
        {"MSG_INVALID_VALUE", "That value is not allowed"},
        {"MSG_FILE_TOO_LARGE", "That file is too large to read"},
        {"MSG_LIST_CUT", "Only the first 20,000 entries were read"},
        {"MSG_EXPORTED", "Exported to {path}"},
        {"MSG_FETCHED_SOME", "Fetched {n}."},             // then " m remain" when some remain
        {"MSG_HVSC_READY", "HVSC #{n} is ready"},
        {"MSG_HVSC_FAILED", "HVSC could not be set up: {step}"},
        {"MSG_HVSC_NEWEST", "HVSC #{n} is the newest release"},
        {"MSG_HVSC_NEWER", "HVSC #{m} is available"},
        {"MSG_DOWNLOAD_DONE", "All of HVSC is on the device"},
        {"MSG_DOWNLOAD_FAILED", "The download failed: {step}"},
        {"MSG_UNPACKING", "HVSC is being unpacked. Playing is off until it is done"},
        {"MSG_NEED_ROOM", "Not enough room: {n} MiB needed, {m} MiB free"},
        {"MSG_NEED_7ZIP", "Install the 7zip package to download everything."},
        {"MSG_NEED_MEMORY", "This device does not have enough free memory to unpack HVSC ({n} MiB free, 450 MiB needed)."},
        {"MSG_NEED_WRITE", "The folder that holds the collection cannot be written."},
        {"MSG_CHECK_FAILED", "Could not check for a newer release"},
        {"MSG_IMPORT_NOTHING", "Nothing in that file could be read as a playlist"},
        {"MSG_INDEX_READY", "The search index is ready"},
        {"MSG_SELFTEST", "Self-test running: leave the device alone"},
    };
    return words;
}

bool all_digits(const std::string& text) {
    if (text.empty()) return false;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

}  // namespace

std::string message_words(const std::string& id, const std::map<std::string, std::string>& about) {
    const auto found = words_table().find(id);
    if (found == words_table().end()) return id;
    if (id == "MSG_VOLUME" && about.find("n") == about.end()) return "Muted";

    const std::string& words = found->second;
    std::string out;
    std::size_t i = 0;
    while (i < words.size()) {
        if (words[i] != '{') {
            out += words[i++];
            continue;
        }
        const std::size_t close = words.find('}', i);
        const std::string name = words.substr(i + 1, close - i - 1);
        i = close + 1;
        const auto value = about.find(name);
        if (value == about.end()) {
            if (!out.empty() && out.back() == ' ') out.pop_back();   // left out, with the space before it
            continue;
        }
        out += name == "step" && all_digits(value->second) ? "step " + value->second : value->second;
    }
    if (id == "MSG_FETCHED_SOME") {
        const auto remain = about.find("m");
        if (remain != about.end() && !remain->second.empty() && remain->second != "0") {
            out += " " + remain->second + " remain";
        }
    }
    return out;
}

}  // namespace spz
