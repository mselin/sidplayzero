// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/hvsc_screen.h"

#include <algorithm>
#include <cstdio>
#include <exception>

#include "spz/complete_download.h"
#include "spz/hvsc_setup.h"
#include "spz/index.h"
#include "spz/listing.h"
#include "spz/messages.h"
#include "spz/releases.h"
#include "ui/draw.h"
#include "ui/panel.h"

namespace spz::ui {

namespace {

const char* const kGet = "Get HVSC";
const char* const kCheck = "Check for a newer release";
const char* const kUpdate = "Update to #";
const char* const kDownload = "Download everything";
const char* const kFinish = "Finish the download";
const char* const kRebuild = "Rebuild the index";

const int kLineH = 18;           // a line of the status block and of the explanation
const int kRowsTop = 2 * kLineH + 2;
const int kTextX = 4;
const int kTextW = kWidth - 8;

std::string size_text(std::uint64_t bytes) {
    char text[32];
    if (bytes < 1024 * 1024) {
        std::snprintf(text, sizeof text, "%llu KiB", static_cast<unsigned long long>((bytes + 1023) / 1024));
    } else {
        std::snprintf(text, sizeof text, "%.1f MiB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    }
    return text;
}

bool unpacking(const JobState& s) { return s.kind == JobKind::Download && s.step == 5; }

}  // namespace

std::string job_step_text(const JobState& s) {
    switch (s.kind) {
        case JobKind::Setup:
        case JobKind::Update:
            switch (s.step) {
                case 2: return "Checking the room on the card";
                case 3: return "Downloading HVSC's documents";
                case 4: return "Checking the documents";
                case 5: return "Moving the documents into place";
                case 6: return "Building the listing";
                default: return s.kind == JobKind::Setup ? "Setting HVSC up" : "Updating HVSC";
            }
        case JobKind::Download:
            switch (s.step) {
                case 2: return "Checking the memory and the room";
                case 3: return "Downloading the archive";
                case 4: return "Checking the archive";
                case 5: return "Unpacking the archive";
                case 6: return "Checking what was unpacked";
                case 7: return "Moving the files into place";
                case 8: return "Building the listing again";
                default: return "Downloading everything";
            }
        case JobKind::Fetch:
            return "Fetching the playlist's tunes";
        case JobKind::None:
            break;
    }
    return "";
}

std::string job_figures(const JobState& s) {
    if (s.kind == JobKind::None) return "";
    if (s.kind == JobKind::Fetch) {
        if (s.total == 0) return "";
        return std::to_string(s.done) + " of " + std::to_string(s.total);
    }
    if (unpacking(s)) return std::to_string(s.done) + (s.done == 1 ? " file written" : " files written");
    if (s.total > 0) return size_text(s.done) + " of " + size_text(s.total);
    if (s.done > 0) return size_text(s.done);
    return "";
}

Json describe_job(const JobState& s) {
    if (s.kind == JobKind::None) return Json();
    Json job = Json::object();
    job.set("kind", job_kind_name(s.kind));
    job.set("step", s.step);
    job.set("text", job_step_text(s));
    job.set("done", static_cast<std::int64_t>(s.done));
    job.set("total", s.total > 0 ? Json(static_cast<std::int64_t>(s.total)) : Json());
    return job;
}

std::string job_mark(const JobState& s) {
    if (s.kind == JobKind::None) return "";
    return "\xE2\x86\x93 " + std::to_string(job_percent(s)) + " %";   // "↓ n %"
}

HvscScreen::HvscScreen(Shell* shell) : Screen(shell) {}

HvscScreen::~HvscScreen() { lifetime_.end(); }

bool HvscScreen::ready() {
    const Services& s = shell()->services();
    return s.jobs != nullptr && s.setup != nullptr && s.listing != nullptr && s.files != nullptr;
}

JobState HvscScreen::job() const {
    const Jobs* jobs = shell()->services().jobs;
    return jobs ? jobs->state() : JobState();
}

bool HvscScreen::job_running() const { return job().kind != JobKind::None; }

void HvscScreen::look_again() {
    // SPEC-140, SPEC-145, SPEC-148: what is known of HVSC, read on the quick worker (it reads hv_sids.txt,
    // the listing and the index, and asks the seams for memory, room and a 7z).
    Services& s = shell()->services();
    seen_changes_ = s.hvsc_changes;
    if (looking_) {
        look_wanted_ = true;
        return;
    }
    looking_ = true;
    look_wanted_ = false;
    Files* files = s.files;
    const Listing* listing = s.listing;
    const Index* index = s.index;
    Unpacker* unpacker = s.unpacker;
    Memory* memory = s.memory;
    const bool download = s.download != nullptr;
    const std::string root = s.collection;
    Shell* frame = shell();
    const Lifetime::Token alive = lifetime_.token();
    frame->quick()->post([this, frame, alive, files, listing, index, unpacker, memory, download, root] {
        Facts found;
        found.known = true;
        try {
            const HvscStatus status = hvsc_status(files, *listing, root);
            found.set_up = status.set_up;
            found.release = status.release;
            found.listed = listing->is_open() ? listing->tune_count() : 0;
            if (index) {
                const int count = index->count();
                const int batch = 2000;
                for (int first = 0; first < count; first += batch) {
                    for (const IndexRecord& record : index->records(first, batch)) {
                        if (record.present) ++found.present;
                    }
                }
            }
            if (download && status.set_up && status.release > 0 && unpacker && memory) {
                const DownloadOffer offer = download_offer(status, files, unpacker, memory, root);
                found.offered = offer.offered;
                if (!offer.offered && !offer.text.empty()) {
                    found.why_not = message_words(offer.text, offer.about);
                }
            }
        } catch (const std::exception& error) {
            std::fprintf(stderr, "sidplayzero: reading what is known of HVSC failed: %s\n", error.what());
        }
        frame->ui()->post([this, frame, alive, found] {
            if (!alive.alive()) return;
            facts_ = found;
            looking_ = false;
            if (look_wanted_) look_again();
            frame->redraw();
        });
    });
}

std::vector<Row> HvscScreen::wanted_rows() const {
    // SPEC-102's table, in its order.
    const Services& s = shell()->services();
    std::vector<std::string> texts;
    // Nothing until HVSC has been looked at, so that the first row is highlighted when it comes.
    if (!facts_.known) return {};
    {
        if (!facts_.set_up) texts.push_back(kGet);
        if (facts_.set_up && s.releases) texts.push_back(kCheck);
        if (facts_.set_up && s.releases && s.releases->newer() > 0) {
            texts.push_back(kUpdate + std::to_string(s.releases->newer()));
        }
        if (s.download && s.download->unfinished()) {
            texts.push_back(kFinish);
        } else if (s.download && facts_.set_up && facts_.release > 0 && facts_.offered) {
            texts.push_back(kDownload);
        }
    }
    texts.push_back(kRebuild);
    std::vector<Row> rows;
    for (const std::string& text : texts) {
        Row row;
        row.text = text;
        row.kind = "action";
        rows.push_back(row);
    }
    return rows;
}

void HvscScreen::sync() const {
    std::vector<Row> rows = wanted_rows();
    std::vector<std::string> texts;
    for (const Row& row : rows) texts.push_back(row.text);
    if (texts == row_texts_) return;
    // Keep the highlight on the row it was on, if that row is still there; else at the same place.
    const std::string was = list_.size() > 0 ? list_.rows()[list_.highlighted()].text : "";
    const std::size_t at = list_.highlighted();
    row_texts_ = texts;
    list_.set_rows(std::move(rows));
    const auto same = std::find(texts.begin(), texts.end(), was);
    if (!was.empty() && same != texts.end()) list_.highlight(static_cast<std::size_t>(same - texts.begin()));
    else list_.highlight(std::min(at, texts.empty() ? 0 : texts.size() - 1));
}

std::vector<std::string> HvscScreen::status_lines() const {
    // The first line, the index's state (drawn on the right of the first line), and the tunes.
    std::vector<std::string> lines;
    if (!facts_.known) lines.push_back("");
    else if (!facts_.set_up) lines.push_back("HVSC is not set up");
    else if (facts_.release > 0) lines.push_back("HVSC #" + std::to_string(facts_.release));
    else lines.push_back("HVSC, release not known");
    const Index* index = shell()->services().index;
    if (!index) lines.push_back("");
    else if (index->complete()) lines.push_back("Index ready");
    else lines.push_back("Indexing " + std::to_string(index->percent()) + " %");
    if (!facts_.known) lines.push_back("");
    else if (facts_.set_up) {
        lines.push_back(std::to_string(facts_.listed) + " tunes in HVSC, " + std::to_string(facts_.present) +
                        " on the device");
    } else {
        lines.push_back(std::to_string(facts_.present) + (facts_.present == 1 ? " tune" : " tunes") + " on the device");
    }
    return lines;
}

std::vector<std::string> HvscScreen::job_lines() const {
    const JobState s = job();
    if (s.kind == JobKind::None) return {};
    std::string figures = job_figures(s);
    if (s.total > 0 && !unpacking(s)) figures += (figures.empty() ? "" : "  ") + std::to_string(job_percent(s)) + " %";
    return {job_step_text(s), figures};
}

std::vector<std::string> HvscScreen::explanation() const {
    // SPEC-145: why the complete download is not offered, and what to do instead.
    const Services& s = shell()->services();
    if (job_running() || !s.download || s.download->unfinished() || facts_.why_not.empty()) return {};
    return {facts_.why_not,
            "Tunes are fetched as you play them. To have everything, copy a C64Music folder into " + s.collection};
}

std::array<std::string, 5> HvscScreen::actions() const {
    std::array<std::string, 5> labels = action_labels(place());
    // SPEC-102: key 5 is "Stop" while a job runs that can still be stopped (SPEC-144, step 7).
    const JobState s = job();
    if (s.kind == JobKind::None || !s.can_stop) labels[1] = "";
    return labels;
}

std::string HvscScreen::signature() const {
    std::string text;
    for (const std::string& line : status_lines()) text += line + "\n";
    for (const std::string& line : job_lines()) text += line + "\n";
    for (const std::string& line : explanation()) {
        text += line + "\n";
        const int overflow = text_width(line, text_font()) - kTextW;
        if (overflow > 0) text += std::to_string(scroll_offset(overflow, shell()->clock()->now_ms())) + "\n";
    }
    for (const Row& row : wanted_rows()) text += row.text + "\n";
    const JobState s = job();
    text += std::to_string(static_cast<int>(s.kind)) + " " + std::to_string(s.step) + " " + std::to_string(s.done) +
            " " + std::to_string(s.total) + (s.can_stop ? " stop" : "");
    return text;
}

void HvscScreen::shown() {
    look_again();
    shell()->redraw();
}

void HvscScreen::tick() {
    if (shell()->services().hvsc_changes != seen_changes_) look_again();
    if (signature() != drawn_) shell()->redraw();
}

void HvscScreen::draw(lv_obj_t* content) {
    if (shell()->services().hvsc_changes != seen_changes_) look_again();
    drawn_ = signature();
    sync();
    const std::vector<std::string> status = status_lines();
    make_label(content, "status", status[0], kTextX, 0, 200, kLineH);
    if (!status[1].empty()) {
        make_label(content, "index", status[1], kTextX + 204, 0, kTextW - 204, kLineH,
                   LabelStyle{Align::Right, nullptr, true, false});
    }
    make_label(content, "tunes", status[2], kTextX, kLineH, kTextW, kLineH);

    if (job_running()) {
        // SPEC-147: the step in words, the figures, and a bar.
        const JobState s = job();
        const std::vector<std::string> lines = job_lines();
        make_label(content, "job.step", lines[0], kTextX, kRowsTop + 2, kTextW, kLineH);
        make_label(content, "job.figures", lines[1], kTextX, kRowsTop + 2 + kLineH, kTextW, kLineH);
        const int bar_y = kRowsTop + 2 + 2 * kLineH + 4;
        make_fill(content, kTextX, bar_y, kTextW, 6, colour_line());
        if (s.total > 0 && !unpacking(s)) {
            const int filled = static_cast<int>(static_cast<long long>(kTextW) * job_percent(s) / 100);
            if (filled > 0) make_fill(content, kTextX, bar_y, filled, 6, colour_highlight());
        }
        return;
    }

    const int shown = std::min(static_cast<int>(list_.size()), (kContentHeight - kRowsTop) / kRowHeight);
    list_.set_shown(std::max(shown, 1));
    list_.draw(content, kRowsTop);
    std::vector<std::string> words = explanation();
    if (words.empty()) return;
    int y = kRowsTop + shown * kRowHeight;
    // Two lines where there is room for them, else one.
    if (kContentHeight - y < 2 * kLineH) words = {words[0] + " " + words[1]};
    for (std::size_t i = 0; i < words.size(); ++i) {
        LabelStyle style;
        style.dim = true;
        const int overflow = text_width(words[i], text_font()) - kTextW;
        style.scroll = overflow > 0 ? scroll_offset(overflow, shell()->clock()->now_ms()) : -1;
        make_label(content, "why" + std::to_string(i + 1), words[i], kTextX, y, kTextW, kLineH, style);
        y += kLineH;
    }
}

void HvscScreen::run_highlighted() {
    sync();
    if (list_.size() == 0) return;
    const std::string chosen = list_.rows()[list_.highlighted()].text;
    Services& s = shell()->services();
    if (chosen == kGet) {
        if (s.setup) s.setup->start();
    } else if (chosen == kCheck) {
        if (s.releases) s.releases->check();
    } else if (chosen.compare(0, std::string(kUpdate).size(), kUpdate) == 0) {
        if (s.releases) s.releases->update();
    } else if (chosen == kDownload) {
        if (s.download) s.download->start();
    } else if (chosen == kFinish) {
        if (s.download) s.download->finish();
    } else if (chosen == kRebuild) {
        if (s.rebuild_index) s.rebuild_index();
    }
}

bool HvscScreen::on_action(const KeyEvent& event) {
    const std::string& a = event.action;
    if (a == "stop_job") {
        // SPEC-147: "Stop" asks first; Enter stops the job, ESC lets it go on.
        const JobState s = job();
        if (s.kind == JobKind::None || !s.can_stop) return true;
        Shell* frame = shell();
        frame->open_panel(std::make_unique<ConfirmPanel>(frame, "Stop the download?", [frame] {
            if (Jobs* jobs = frame->services().jobs) jobs->stop();
            frame->redraw();
        }));
        return true;
    }
    if (job_running()) {
        // SPEC-102: while a job runs the list is replaced by the progress, and Enter does nothing.
        return a == "run" || list_.on_action(a);
    }
    sync();
    if (list_.on_action(a)) {
        shell()->redraw();
        return true;
    }
    if (a == "run") {
        Shell* frame = shell();
        run_highlighted();
        frame->redraw();
        return true;
    }
    if (a == "back") {
        shell()->close_top();
        return true;
    }
    return false;
}

void HvscScreen::describe(Json* description) const {
    const JobState s = job();
    std::vector<std::string> lines = status_lines();
    if (s.kind != JobKind::None) {
        for (const std::string& line : job_lines()) lines.push_back(line);
    } else {
        sync();
        description->set("rows", list_.describe_rows());
        description->set("list", list_.describe_list());
        for (const std::string& line : explanation()) lines.push_back(line);
    }
    Json array = Json::array();
    for (const std::string& line : lines) array.push(line);
    description->set("lines", array);
    description->set("job", describe_job(s));
}

}  // namespace spz::ui
