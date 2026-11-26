// SPDX-License-Identifier: GPL-3.0-or-later
// Every screen of the app, by its name in SPEC-090 and SPEC-254. An area that builds a screen adds one
// line here (see the guide at the top of ui/screen.h). A screen that is not listed cannot be opened: its
// keys do nothing.
#include <memory>

#include "app/app.h"
#include "ui/about_screen.h"
#include "ui/browser_screen.h"
#include "ui/empty_screen.h"
#include "ui/playing_screen.h"
#include "ui/settings_screen.h"

namespace spz::app {

namespace {

template <typename T>
ScreenMaker maker() {
    return [](ui::Shell* shell) { return std::unique_ptr<ui::Screen>(new T(shell)); };
}

}  // namespace

void add_screens(ScreenList* list) {
    list->add("browser", maker<ui::BrowserScreen>());
    list->add("empty", maker<ui::EmptyScreen>());
    list->add("playing", maker<ui::PlayingScreen>());
    list->add("settings", maker<ui::SettingsScreen>());
    list->add("about", maker<ui::AboutScreen>());
    // To come, from the areas that build them, one line each: "search", "playlists", "playlist", "hvsc",
    // "selftest".
}

}  // namespace spz::app
