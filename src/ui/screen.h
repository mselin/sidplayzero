// SPDX-License-Identifier: GPL-3.0-or-later
// A screen of the app (SPEC-090), and what it may ask of the app's frame.
//
// How to add a screen:
//
// 1. Write a class derived from ui::Screen, in src/ui/NAME_screen.{h,cpp}. Give it:
//    - name(): its name in the screen description and the stack (SPEC-254): "browser", "playing", ...
//    - place(): the place of docs/keymap.tsv whose keys apply while it is on top. It is name() unless the
//      screen has two halves (Search: "search-field" while its field is in use, "search-results" else).
//    - title(), position(): the left of the top line (SPEC-091). position() is "n/total" or "".
//    - draw(content): make its LVGL objects in `content`, the content band (320×120, its top at y = 16).
//      Use ui::make_label() (ui/draw.h) for every piece of text: it names the box, cuts the text to fit and
//      marks it as cut, as SPEC-098 and the screen description want. A list uses ui::RowList (ui/rows.h),
//      which draws rows of 20 pixels, moves its highlight with the `lists` keys and describes itself.
//      The frame calls draw() after any change that you announce with shell()->redraw(); it throws away
//      the objects of the time before, so keep the screen's state in members and never in LVGL objects.
//    - on_action(event): what a key does there. `event.action` is the key map's word for the key in the
//      screen's place ("open_or_play", "up" ...). Return true when the screen did something with it. The
//      frame first deals with what is the same on every screen (help, ESC, held ESC, TAB, P, the screen
//      keys of SPEC-090's table, brightness, the screenshot) and with the actions that other areas have
//      registered with Shell::on_action() (play or pause, the volume ...); the screen gets the rest.
//      A short ESC that would close the bottom screen comes to that screen as "back" (the browser's
//      "parent folder").
//    - describe(d): fill in the fields of SPEC-254 that are the screen's own: `rows`, `list`, `detail`,
//      `lines`, `fields`, `voices`, `job` and the like. The frame fills `screen`, `stack`, `place`,
//      `title`, `position`, `actions`, `typing`, `panel`, `help`, `notice`, `text` and `boxes`.
//    - actions(): the five labels over the keys 4 to 8. The key map's, unless the screen takes one away
//      while its action cannot be used (SPEC-095: "Playing" with no current entry).
//    - ready(): false while what the screen cannot do without is not there (the composition has not set
//      it in Services, or this build has no such part). A key that would open a screen that is not ready
//      does nothing.
// 2. Add one line for it to src/app/screens.cpp. Nothing else needs to change: the key map already has
//    its keys, and opening it by its key (key 5 in the browser, "open_search") is the frame's work.
// 3. Panels are ui::Panel (ui/panel.h): open one with shell()->open_panel(). Notices: shell()->notice().
//    Work that may take longer than 50 ms goes to a worker (shell()->quick(), fetch_worker(), long_worker()),
//    and its result comes back with shell()->ui()->post(), which runs it on the UI thread.
// 4. What is not one screen's own goes to the Shell, from the app's composition (src/app/compose_*.cpp):
//    Shell::on_action() for an action that is the same on every screen (the player's "play_pause", the
//    volume keys), add_describer() for the fields of every description (`player`, `mini`, `job`),
//    add_top_right() for the mini player and the job mark, every() for what changes by itself,
//    when_no_overlay() for what must wait until no panel and no help is open (SPEC-113), and set_bottom()
//    for the browser taking the empty screen's place (SPEC-048). The objects of `core` a screen uses are in
//    Shell::services(), which the composition fills in; a screen asks for them each time, and keeps none.
// 5. Everything here runs on the UI thread.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "lvgl.h"
#include "spz/clock.h"
#include "spz/executor.h"
#include "spz/files.h"
#include "spz/json.h"
#include "spz/key_input.h"
#include "spz/keymap.h"
#include "spz/types.h"

namespace spz {
class SettingsFile;
class StateFile;
class NoticeGate;
class Backlight;
class Player;
class SoundBackend;
class BrowserModel;
class Collection;
class Listing;
class Fetcher;
class Index;
class Jobs;
class HvscSetup;
class Releases;
class CompleteDownload;
class Playlists;
class Favourites;
class FetchMissing;
class Metrics;
class ScreenOff;
class Unpacker;
class Memory;
struct Entry;
}  // namespace spz

namespace spz::ui {

class Panel;
class SelfTestView;

// The objects of `core` and `platform` the screens share, set by the app's composition (src/app/
// composition*.cpp), each by the file of its area. A pointer is null while what it points at is not there
// (a part that is not built, or, for the browser, a collection that is being set up afresh): a screen asks
// for it each time it needs it, and never keeps it.
struct Services {
    // ---- the outside world, and what is the same for every screen
    Files* files = nullptr;
    Backlight* backlight = nullptr;       // SPEC-104
    std::string home;                     // the home folder
    std::string collection;               // the collection root (SPEC-040), as a full path
    bool collection_from_variable = false;   // the root comes from HVSC_BASE (SPEC-040)
    std::vector<std::string> libraries;   // "libsidplayfp 3.1.2", "libresidfp 1.3.0": read from the libraries

    // ---- keep (SPEC-130 to SPEC-135, SPEC-207, SPEC-241): src/app/compose_keep.cpp
    SettingsFile* settings = nullptr;     // SPEC-130
    StateFile* state = nullptr;           // SPEC-131
    Metrics* metrics = nullptr;           // SPEC-241
    ScreenOff* screen_off = nullptr;      // SPEC-207, part `screen-off`
    // A setting has been changed through `settings` (the Settings screen calls this after every change
    // it makes): what follows it at once, follows (SPEC-040, SPEC-065, SPEC-207 ...).
    std::function<void(const std::string& key)> setting_changed;

    // ---- the collection and the browser (SPEC-040 to SPEC-049): src/app/compose_browse.cpp
    Collection* tree = nullptr;           // the collection itself (the root's folders and the listing's)
    BrowserModel* browser = nullptr;      // the folder open in the browser, kept while the app runs
    const Listing* listing = nullptr;     // the listing in use (SPEC-142); never null, maybe not open

    // ---- playing (SPEC-060 to SPEC-080): src/app/compose_play.cpp
    Player* player = nullptr;
    SoundBackend* sound = nullptr;        // what the player plays through: for the voice display (SPEC-080)
    // "Play an entry": the queue is `queue`, the entry at `chosen` plays, the state file records where the
    // queue came from (SPEC-131: "folder", "search" or "playlist", and the folder, query or playlist name),
    // and the now-playing screen opens (SPEC-090).
    std::function<void(const std::vector<Entry>& queue, std::size_t chosen, const std::string& kind,
                       const std::string& source)> play;

    // ---- HVSC (SPEC-140 to SPEC-154): src/app/compose_hvsc.cpp
    NoticeGate* notice_gate = nullptr;    // SPEC-152
    Jobs* jobs = nullptr;                 // SPEC-147
    Fetcher* fetcher = nullptr;           // SPEC-141
    HvscSetup* setup = nullptr;           // SPEC-140
    Releases* releases = nullptr;         // SPEC-148, part `releases`
    CompleteDownload* download = nullptr; // SPEC-144, part `download-all`
    Unpacker* unpacker = nullptr;         // SPEC-145's conditions: a `7z` on the path
    Memory* memory = nullptr;             // SPEC-145's conditions: memory and swap
    // Counts the ends of set-ups, updates and complete downloads, however they ended: the HVSC screen
    // looks at HVSC again when it changes.
    std::uint64_t hvsc_changes = 0;

    // ---- search (SPEC-160 to SPEC-164): src/app/compose_search.cpp
    Index* index = nullptr;
    // "Rebuild the index" (SPEC-102, SPEC-161): the index is thrown away and built again on the long worker,
    // and MSG_INDEX_READY tells when it is done.
    std::function<void()> rebuild_index;

    // ---- playlists and favourites (SPEC-170 to SPEC-179), part `playlists`: src/app/compose_lists.cpp
    Playlists* playlists = nullptr;
    Favourites* favourites = nullptr;
    FetchMissing* fetch_missing = nullptr;
    // "Add" on a tune (the browser, Search, now playing): asks which playlist (`pick-playlist`) and adds
    // it there. Null while the part's panel is not built.
    std::function<void(const Entry& entry)> add_to_playlist;
    std::string import_folder;            // SPEC-134, SPEC-175: the `import` folder
    std::string export_folder;            // SPEC-134, SPEC-179: the `export` folder
    std::string open_playlist;            // the playlist the Playlist screen shows: Enter in Playlists sets it
    // Counts changes to the playlists that come by themselves (a tune of "Fetch missing" arrived): a screen
    // that shows them draws again when it changes.
    int lists_changes = 0;

    // ---- the self-test (SPEC-260): src/app/compose_selftest.cpp
    // What the self-test screen shows; null when no device test asks for a step, and the screen cannot open.
    SelfTestView* selftest = nullptr;
};

// What a screen or a panel may ask of the app's frame.
class Shell {
public:
    virtual ~Shell() = default;

    virtual Clock* clock() = 0;
    // SPEC-022: the UI thread's queue, and the workers.
    virtual Executor* ui() = 0;
    virtual Executor* quick() = 0;
    virtual Executor* fetch_worker() = 0;
    virtual Executor* long_worker() = 0;
    virtual Services& services() = 0;

    // SPEC-113: raises a notice of SPEC-116, its words filled in by spz::message_words().
    virtual void notice(const std::string& id, const std::map<std::string, std::string>& about = {}) = 0;
    // The answer to a key the user has just pressed, when it comes later than the key (from a worker): it
    // shows at once, in place of the notice showing (SPEC-113, FIND-025). A notice raised while a key is
    // being handled is such an answer already.
    virtual void answer(const std::string& id, const std::map<std::string, std::string>& about = {}) = 0;
    // SPEC-090: opens a screen by its name; if it is in the stack already, returns to it. False if there is
    // no such screen in this build, or it is not ready.
    virtual bool open(const std::string& screen) = 0;
    // SPEC-048: which of the two the bottom of the stack is, "browser" or "empty" (the browser's area decides).
    virtual void set_bottom(const std::string& screen) = 0;
    // A short ESC, or "Back": closes the top screen.
    virtual void close_top() = 0;
    // SPEC-114: one panel at a time. Opening one replaces any that is open.
    virtual void open_panel(std::unique_ptr<Panel> panel) = 0;
    virtual void close_panel() = 0;
    virtual Panel* panel() = 0;
    // Whether help is open (SPEC-107). With panel(), what SPEC-113's two exceptions wait for: the browser
    // that takes the empty screen's place, and the browser that opens when HVSC has been set up, wait until
    // neither is open: when_no_overlay() is for that.
    virtual bool help_open() const = 0;
    // Runs `fn` once, the next time no panel and no help is open (at once if none is).
    virtual void when_no_overlay(std::function<void()> fn) = 0;
    // The state of a screen or a panel changed: draw everything again before the next frame.
    virtual void redraw() = 0;
    // What on_action(), add_describer(), add_top_right() and every() return: given to remove(), it undoes
    // what they added. Whoever adds something that holds an object (a screen that passes `this`) removes
    // it before that object goes. 0 is no hook: what was refused.
    using Hook = std::uint64_t;
    // For the areas whose actions are the same on every screen (the player's "play_pause", the volume's
    // "volume_up" ...): `handler` is called for every key whose action is `action`, wherever it is pressed.
    // An action has at most one handler: a second is refused (0), and the first stays.
    virtual Hook on_action(const std::string& action, std::function<void(const KeyEvent&)> handler) = 0;
    // For what shows on every screen, not one screen's own: the player's `player` and the mini player's and
    // the job mark's `mini` (SPEC-109, SPEC-147, SPEC-254). `fn` fills its fields of every description.
    virtual Hook add_describer(std::function<void(Json*)> fn) = 0;
    // The right part of the top line (SPEC-091): the mini player and the job mark. Each `text` added gives
    // its piece ("" for nothing); the frame joins the pieces, in the order added, draws them right-aligned in
    // a box named "mini", and gives the left part what is left. Asked at every drawing; call redraw() when
    // one changes. On the now-playing screen nothing is drawn there (SPEC-109).
    virtual Hook add_top_right(std::function<std::string()> text) = 0;
    // Runs `fn` again and again, every `ms` of the clock, on the UI thread, while the app runs: for what is
    // shown that changes by itself (the elapsed time, the voice display, a job's progress).
    virtual Hook every(Millis ms, std::function<void()> fn) = 0;
    // Undoes what a hook added; nothing if it is 0 or undone already. `fn` of every() may undo its own hook.
    virtual void remove(Hook hook) = 0;
};

class Screen {
public:
    explicit Screen(Shell* shell) : shell_(shell) {}
    virtual ~Screen() = default;
    Screen(const Screen&) = delete;
    Screen& operator=(const Screen&) = delete;

    virtual std::string name() const = 0;
    virtual std::string place() const { return name(); }
    virtual bool ready() { return true; }
    virtual std::string title() const = 0;
    virtual std::string position() const { return ""; }
    virtual std::array<std::string, 5> actions() const { return action_labels(place()); }
    // Whether text is typed into the screen itself (the Search field), not into a panel.
    virtual bool typing() const { return false; }
    virtual void draw(lv_obj_t* content) = 0;
    virtual bool on_action(const KeyEvent& event) = 0;
    virtual void describe(Json* description) const { (void)description; }
    // It has come on top of the stack: opened, or returned to.
    virtual void shown() {}
    // About 30 times a second of the clock while it is on top and no panel or help is open: for what changes
    // by itself (the elapsed time, the voice display, a line that scrolls). Call shell()->redraw() only when
    // what is drawn would change.
    virtual void tick() {}

protected:
    Shell* shell() const { return shell_; }

private:
    Shell* shell_;
};

}  // namespace spz::ui
