// SPDX-License-Identifier: GPL-3.0-or-later
// The panels of the part `playlists` (SPEC-114, SPEC-172, SPEC-175, SPEC-178): `pick-playlist`, which "Add"
// opens in the browser, Search and the now-playing "More" panel; the prompt for a new playlist's name; and
// `import`, with the `text` panel that tells how an import went. The Playlists object they change is the
// composition's (ui::Services::playlists), and is only touched on the UI thread, as the browser's "Fav" does.
#pragma once

#include <functional>
#include <string>

#include "spz/types.h"
#include "ui/screen.h"

namespace spz::ui {

// SPEC-172: "Add". Lists the playlists and "New playlist…"; Enter adds `entry` to the one chosen, or asks
// for a new playlist's name, makes it and adds the entry there. The notice MSG_ADDED or MSG_ALREADY_THERE
// tells what happened.
void open_pick_playlist(Shell* shell, const Entry& entry);

// SPEC-172: the prompt for a new playlist's name. A name that is taken gives MSG_NAME_TAKEN, one that is not
// allowed MSG_INVALID_VALUE, and the prompt stays open; once one is made, `made` runs with its name.
void open_new_playlist(Shell* shell, std::function<void(const std::string& name)> made = nullptr);

// SPEC-175: the `import` panel. The files that can be imported are listed by the quick worker; Enter imports
// the one chosen into a new playlist, and a `text` panel says how many entries were imported and which
// were not found (SPEC-178).
void open_import(Shell* shell);

}  // namespace spz::ui
