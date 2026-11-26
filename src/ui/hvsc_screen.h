// SPDX-License-Identifier: GPL-3.0-or-later
// The HVSC screen (SPEC-102): a status block (the release, how many tunes HVSC lists and how many are on
// the device, the index's state) and under it the things to do, chosen with Up, Down and Enter: "Get
// HVSC" (SPEC-140), "Check for a newer release" and "Update to #m" (SPEC-148, part `releases`), "Download
// everything" or "Finish the download" (SPEC-144, part `download-all`), with SPEC-145's explanation under
// the list when the complete download is not offered, and "Rebuild the index" (SPEC-161). While a job
// runs (SPEC-147) the list is replaced by its step, its figures and a bar, and key 5 is "Stop".
//
// What it shows of HVSC is read on the quick worker when the screen opens and whenever a set-up, an
// update or a complete download has ended (Services::hvsc_changes); the job and the index are asked
// each time.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "spz/job.h"
#include "spz/lifetime.h"
#include "ui/rows.h"
#include "ui/screen.h"

namespace spz::ui {

// SPEC-147: the step of a job in words, for the HVSC screen and the description's `job.text`.
std::string job_step_text(const JobState& state);
// SPEC-147: the job's figures: bytes done and total, files written while 7z unpacks, tunes for "Fetch
// missing". "" while there is nothing to tell.
std::string job_figures(const JobState& state);
// SPEC-254: the description's `job`, or null when no job runs.
Json describe_job(const JobState& state);
// SPEC-147: the job mark, an arrow and the job's percentage; "" when no job runs.
std::string job_mark(const JobState& state);

class HvscScreen : public Screen {
public:
    explicit HvscScreen(Shell* shell);
    ~HvscScreen() override;
    std::string name() const override { return "hvsc"; }
    // It needs the jobs and the set-up of the HVSC area.
    bool ready() override;
    std::string title() const override { return "HVSC"; }
    std::array<std::string, 5> actions() const override;
    void draw(lv_obj_t* content) override;
    bool on_action(const KeyEvent& event) override;
    void describe(Json* description) const override;
    void shown() override;
    void tick() override;

private:
    // What is read of HVSC on the quick worker.
    struct Facts {
        bool known = false;                 // false until the first reading has come back
        bool set_up = false;
        int release = 0;
        int listed = 0;                     // tunes HVSC lists
        int present = 0;                    // tunes on the device
        bool offered = false;               // SPEC-145 allows the complete download
        std::string why_not;                // SPEC-145's explanation, or ""
    };

    void look_again();                      // reads the facts on the quick worker
    JobState job() const;
    bool job_running() const;
    std::vector<Row> wanted_rows() const;
    void sync() const;                      // the rows as the state now has them, the highlight kept
    std::vector<std::string> status_lines() const;
    std::vector<std::string> job_lines() const;
    std::vector<std::string> explanation() const;
    std::string signature() const;
    void run_highlighted();

    Facts facts_;
    std::uint64_t seen_changes_ = 0;
    bool looking_ = false;
    bool look_wanted_ = false;
    Lifetime lifetime_;
    mutable RowList list_;
    mutable std::vector<std::string> row_texts_;
    std::string drawn_;
};

}  // namespace spz::ui
