// SPDX-License-Identifier: GPL-3.0-or-later
// The messages of SPEC-116. Everywhere in `core`, and in the tests, a message is named by its identifier,
// such as "MSG_INVALID_SID"; its words live in one place.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace spz {

enum class MessageKind {
    State,    // shown on the now-playing screen and in the mini player while the player is Failed
    Notice,   // shown for a few seconds without taking the keys (SPEC-113)
    Text,     // shown on the screen or in the panel that asks
};

struct MessageRow {
    const char* id;
    MessageKind kind;
};

// Every row of SPEC-116's table, in its order.
const std::vector<MessageRow>& message_table();

// Whether there is a message with this identifier.
bool is_message(const std::string& id);

// The words of a message, with what it is about filled in: for example {"n", "45"} for MSG_VOLUME gives
// "Volume 45 %". Which names each message takes is in SPEC-116.
std::string message_words(const std::string& id, const std::map<std::string, std::string>& about = {});

}  // namespace spz
