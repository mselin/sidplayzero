// SPDX-License-Identifier: GPL-3.0-or-later
// The browser's model (SPEC-043 to SPEC-045, SPEC-095): which folder is open, its rows, and where the
// highlight is. It lives on the UI thread. A folder's entries are read by the quick worker and arrive
// as a message on the UI thread; the screen draws only the rows it shows, asking for each by its number.
#pragma once

#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "spz/clock.h"
#include "spz/collection.h"
#include "spz/executor.h"
#include "spz/types.h"

namespace spz {

// What the list area says in place of rows (SPEC-043).
enum class ListNote {
    None,      // the rows are shown
    Reading,   // "Reading…": the folder's entries have been awaited for more than 150 ms
    Empty,     // "(empty)": the folder has no entry, and the position reads "0/0"
};

// Told, on the UI thread, when the screen has something new to draw.
class BrowserListener {
public:
    virtual ~BrowserListener() = default;
    // The folder, its rows, the highlight or the note has changed.
    virtual void browser_changed() = 0;
    // A notice to show (SPEC-113), by its identifier: "MSG_FOLDER_UNREADABLE" (SPEC-041).
    virtual void browser_notice(const std::string& id) = 0;
};

class BrowserModel {
public:
    static constexpr int kFoldersKept = 16;           // SPEC-043: the last 16 folders listed stay in memory
    static constexpr Millis kReadingAfterMs = 150;    // SPEC-043: "Reading…" when a folder takes longer

    // What open() did.
    enum class Opened {
        Nothing,   // nothing is highlighted
        Folder,    // the highlighted folder is being opened
        Tune,      // a tune is highlighted: the caller plays it (SPEC-095), with tunes() as the queue
    };

    // `quick` runs the listing (SPEC-022); `ui` brings its result back to the UI thread; `clock` times
    // "Reading…". The collection must outlive the model.
    BrowserModel(Collection* collection, Executor* quick, Executor* ui, Clock* clock);

    void set_listener(BrowserListener* listener);

    // SPEC-043, SPEC-044: opens a folder ("/" is the root). Its entries are asked of the quick worker,
    // unless the folder is one of the last 16 listed and neither its modification time nor, through
    // tune_changed(), its tunes have changed since. The highlight goes to the entry named `entry`; with
    // no name, or no such entry, to the entry that was highlighted the last time this folder was open in
    // this run, else to the first.
    void show(const std::string& folder, const std::string& entry = std::string());

    // SPEC-044, SPEC-095: Enter or Right. On a folder, opens it; on a tune, does nothing itself.
    Opened open();
    // SPEC-044: Left, or a short ESC: the parent folder, with the folder just left highlighted. False,
    // and nothing happens, at the collection root.
    bool parent();

    // The folder that is open, or being read.
    std::string folder() const;
    ListNote note() const;
    // How many entries the open folder has. 0 while it is being read for the first time.
    int count() const;
    // SPEC-043: one row, by its number from 0, in constant time and from memory: no file, no worker.
    const FolderEntry& row(int index) const;
    // The highlighted row's number from 0, or -1 when the folder has no entry.
    int highlight() const;
    // SPEC-045: the top line's "n/total", n counted from 1; "0/0" for a folder with no entry.
    std::string position() const;
    // SPEC-062: the open folder's tunes, in the order shown, for the queue.
    std::vector<FolderEntry> tunes() const;

    // SPEC-045: moving in the list. Up and down move one row and stop at the ends; a page is the number
    // of rows on the screen (SPEC-091: six, or five when the detail line is built). Key repeat is not
    // done here: each call is one step.
    void set_rows_on_screen(int rows);
    void up();
    void down();
    void page_up();
    void page_down();
    void first();
    void last();

    // SPEC-043: a tune has become present, or has stopped being so, through the app (a fetch, SPEC-141).
    // Its folder is listed again when it is next opened, whatever its modification time says.
    void tune_changed(const std::string& path);

private:
    struct Kept {                 // one of the last folders listed (SPEC-043)
        FolderListing listing;
    };
    void ask(const std::string& folder, bool have_kept, std::int64_t kept_s, std::int32_t kept_ns);
    void arrived(std::uint64_t request, FolderListing listing, bool unchanged);
    void keep(const FolderListing& listing);
    Kept* kept(const std::string& folder);
    void place_highlight(const std::string& wanted);
    void remember_highlight();
    void changed();

    Collection* collection_;
    Executor* quick_;
    Executor* ui_;
    Clock* clock_;
    BrowserListener* listener_ = nullptr;

    std::string folder_ = "/";
    std::vector<FolderEntry> rows_;
    bool loaded_ = false;                 // rows_ are the open folder's
    std::int64_t asked_at_ = 0;           // when its entries were asked for
    bool waiting_ = false;                // an answer from the quick worker is awaited
    std::uint64_t request_ = 0;           // the latest request; older answers are dropped
    std::string wanted_;                  // the entry to highlight when the rows arrive
    int highlight_ = -1;
    int rows_on_screen_ = 6;
    std::list<Kept> kept_;                // the most recently used first
    std::map<std::string, std::string> last_highlight_;   // folder -> name of the entry last highlighted in it
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);   // for jobs that come back after the model is gone
};

}  // namespace spz
