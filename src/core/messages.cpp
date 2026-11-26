// SPDX-License-Identifier: GPL-3.0-or-later
// The table of SPEC-116. The identifiers and kinds are here; the words are not built yet (Phase 5).
#include "spz/messages.h"

#include "spz/not_built.h"

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

std::string message_words(const std::string&, const std::map<std::string, std::string>&) { SPZ_NOT_BUILT("SPEC-116"); }

}  // namespace spz
