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
// "Volume 45 %". Which names each message takes is in SPEC-116; the names are n, m, path, name and step
// (FIND-024):
//     MSG_VOLUME                       n: the level in per cent; without n the words are "Muted"
//     MSG_BRIGHTNESS                   n
//     MSG_MODE                         name: the new value of when_song_ends, in words
//     MSG_SCREENSHOT_SAVED, MSG_EXPORTED   path
//     MSG_ADDED                        name: the playlist's
//     MSG_FETCHED_SOME                 n fetched; m remaining, left out when "0" or not given
//     MSG_HVSC_READY, MSG_HVSC_NEWEST  n: the release; MSG_HVSC_NEWER m
//     MSG_HVSC_FAILED, MSG_DOWNLOAD_FAILED   step: a number reads "step 3"; other text is put as it is
//     MSG_NEED_ROOM                    n MiB needed, m MiB free; MSG_NEED_MEMORY n MiB free
// A name that `about` does not give is left out, with the space before it. An identifier that is not in
// the table gives itself back.
std::string message_words(const std::string& id, const std::map<std::string, std::string>& about = {});

}  // namespace spz
