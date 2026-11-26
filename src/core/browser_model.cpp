// SPDX-License-Identifier: GPL-3.0-or-later
// The browser's model (SPEC-043 to SPEC-045, SPEC-095).
#include "spz/browser_model.h"

#include <algorithm>
#include <utility>

#include "spz/path_text.h"

namespace spz {

namespace {

// The folder a path is in: "/A/B.sid" gives "/A", "/B.sid" gives "/".
}  // namespace

BrowserModel::BrowserModel(Collection* collection, Executor* quick, Executor* ui, Clock* clock)
    : collection_(collection), quick_(quick), ui_(ui), clock_(clock) {}

void BrowserModel::set_listener(BrowserListener* listener) { listener_ = listener; }

void BrowserModel::changed() {
    if (listener_ != nullptr) listener_->browser_changed();
}

BrowserModel::Kept* BrowserModel::kept(const std::string& folder) {
    for (auto it = kept_.begin(); it != kept_.end(); ++it) {
        if (it->listing.folder == folder) {
            kept_.splice(kept_.begin(), kept_, it);
            return &kept_.front();
        }
    }
    return nullptr;
}

void BrowserModel::keep(const FolderListing& listing) {
    kept_.remove_if([&](const Kept& k) { return k.listing.folder == listing.folder; });
    kept_.push_front(Kept{listing});
    while (static_cast<int>(kept_.size()) > kFoldersKept) kept_.pop_back();
}

void BrowserModel::remember_highlight() {
    if (loaded_ && highlight_ >= 0 && highlight_ < static_cast<int>(rows_.size())) {
        last_highlight_[folder_] = rows_[static_cast<std::size_t>(highlight_)].name;
    }
}

void BrowserModel::place_highlight(const std::string& wanted) {
    highlight_ = rows_.empty() ? -1 : 0;
    if (rows_.empty()) return;
    auto find = [this](const std::string& name) {
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            if (rows_[i].name == name) return static_cast<int>(i);
        }
        return -1;
    };
    int at = wanted.empty() ? -1 : find(wanted);
    if (at < 0) {
        const auto last = last_highlight_.find(folder_);
        if (last != last_highlight_.end()) at = find(last->second);
    }
    if (at >= 0) highlight_ = at;
}

void BrowserModel::show(const std::string& folder, const std::string& entry) {
    remember_highlight();
    folder_ = folder.empty() ? "/" : folder;
    wanted_ = entry;
    ++request_;
    if (Kept* k = kept(folder_)) {
        // Shown at once from memory; the quick worker looks whether the folder has changed since.
        rows_ = k->listing.entries;
        loaded_ = true;
        place_highlight(wanted_);
        ask(folder_, true, k->listing.on_device ? k->listing.modified_s : -1, k->listing.modified_ns);
    } else {
        rows_.clear();
        loaded_ = false;
        highlight_ = -1;
        ask(folder_, false, 0, 0);
    }
    changed();
}

void BrowserModel::ask(const std::string& folder, bool have_kept, std::int64_t kept_s, std::int32_t kept_ns) {
    waiting_ = true;
    asked_at_ = clock_->now_ms();
    const std::uint64_t request = request_;
    const Lifetime::Token alive = lifetime_.token();
    Collection* collection = collection_;
    Executor* ui = ui_;
    BrowserModel* self = this;
    quick_->post([collection, ui, self, alive, request, folder, have_kept, kept_s, kept_ns] {
        bool unchanged = false;
        FolderListing listing;
        if (have_kept) {
            std::int64_t s = 0;
            std::int32_t ns = 0;
            const bool on_device = collection->folder_time(folder, &s, &ns);
            unchanged = on_device ? (s == kept_s && ns == kept_ns) : kept_s == -1;
        }
        if (!unchanged) listing = collection->list(folder);
        ui->post([self, alive, request, listing, unchanged]() mutable {
            if (!alive.alive()) return;
            self->arrived(request, std::move(listing), unchanged);
        });
    });
    if (!have_kept) {
        clock_->call_after(kReadingAfterMs + 1, [this, alive, request] {
            if (!alive.alive()) return;
            if (request == request_ && waiting_ && !loaded_) changed();
        });
    }
}

void BrowserModel::arrived(std::uint64_t request, FolderListing listing, bool unchanged) {
    if (request != request_) return;
    waiting_ = false;
    if (unchanged) return;
    if (!listing.notice.empty()) {
        kept_.remove_if([&](const Kept& k) { return k.listing.folder == listing.folder; });
        rows_.clear();
        loaded_ = true;
        highlight_ = -1;
        if (listener_ != nullptr) listener_->browser_notice(listing.notice);
        changed();
        return;
    }
    // A folder shown from memory keeps its highlighted entry when its new rows come.
    std::string wanted = wanted_;
    if (loaded_ && highlight_ >= 0 && highlight_ < static_cast<int>(rows_.size())) {
        wanted = rows_[static_cast<std::size_t>(highlight_)].name;
    }
    keep(listing);
    rows_ = std::move(listing.entries);
    loaded_ = true;
    place_highlight(wanted);
    changed();
}

BrowserModel::Opened BrowserModel::open() {
    if (!loaded_ || highlight_ < 0 || highlight_ >= static_cast<int>(rows_.size())) return Opened::Nothing;
    const FolderEntry& entry = rows_[static_cast<std::size_t>(highlight_)];
    if (!entry.is_folder) return Opened::Tune;
    const std::string child = path_text::collection_child(folder_, entry.name);
    show(child);
    return Opened::Folder;
}

bool BrowserModel::parent() {
    if (folder_ == "/") return false;
    const std::size_t slash = folder_.rfind('/');
    const std::string name = folder_.substr(slash + 1);
    show(path_text::collection_parent(folder_), name);
    return true;
}

std::string BrowserModel::folder() const { return folder_; }

ListNote BrowserModel::note() const {
    if (!loaded_) {
        return waiting_ && clock_->now_ms() - asked_at_ > kReadingAfterMs ? ListNote::Reading : ListNote::None;
    }
    return rows_.empty() ? ListNote::Empty : ListNote::None;
}

int BrowserModel::count() const { return loaded_ ? static_cast<int>(rows_.size()) : 0; }

const FolderEntry& BrowserModel::row(int index) const {
    static const FolderEntry none;
    if (index < 0 || index >= static_cast<int>(rows_.size())) return none;
    return rows_[static_cast<std::size_t>(index)];
}

int BrowserModel::highlight() const { return loaded_ ? highlight_ : -1; }

std::string BrowserModel::position() const {
    if (count() == 0) return "0/0";
    return std::to_string(highlight_ + 1) + "/" + std::to_string(count());
}

std::vector<FolderEntry> BrowserModel::tunes() const {
    std::vector<FolderEntry> out;
    if (!loaded_) return out;
    for (const FolderEntry& entry : rows_) {
        if (!entry.is_folder) out.push_back(entry);
    }
    return out;
}

void BrowserModel::set_rows_on_screen(int rows) { rows_on_screen_ = std::max(1, rows); }

namespace {
int clamp_to(int value, int count) { return count <= 0 ? -1 : std::max(0, std::min(value, count - 1)); }
}  // namespace

void BrowserModel::up() {
    if (count() == 0) return;
    highlight_ = clamp_to(highlight_ - 1, count());
    changed();
}

void BrowserModel::down() {
    if (count() == 0) return;
    highlight_ = clamp_to(highlight_ + 1, count());
    changed();
}

void BrowserModel::page_up() {
    if (count() == 0) return;
    highlight_ = clamp_to(highlight_ - rows_on_screen_, count());
    changed();
}

void BrowserModel::page_down() {
    if (count() == 0) return;
    highlight_ = clamp_to(highlight_ + rows_on_screen_, count());
    changed();
}

void BrowserModel::first() {
    if (count() == 0) return;
    highlight_ = 0;
    changed();
}

void BrowserModel::last() {
    if (count() == 0) return;
    highlight_ = count() - 1;
    changed();
}

void BrowserModel::tune_changed(const std::string& path) {
    const std::string folder = path_text::collection_parent(path);
    kept_.remove_if([&](const Kept& k) { return k.listing.folder == folder; });
}

}  // namespace spz
