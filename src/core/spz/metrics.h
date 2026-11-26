// SPDX-License-Identifier: GPL-3.0-or-later
// The measurements the app takes of itself (SPEC-241), which the budgets of SPEC-240 are held against.
// The app records each as it is taken. Started with `--metrics FILE` it writes them to that file when it
// exits, as `key = value` lines; while a device test asks for it, each is also appended to
// selftest.results as it is taken (SPEC-260). The keys are those of SPEC-240's and SPEC-241's tables and
// no others: the table in metrics.cpp has a row for each.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "spz/files.h"
#include "spz/types.h"

namespace spz {

// The form a measurement's value has.
enum class MetricForm {
    Number,                // digits, with or without a decimal part: "812", "14.20"
    NumberOrNotMeasured,   // a number, or the words "not measured" (SPEC-240: index_tunes_per_second)
    YesNo,                 // "yes" or "no"
    Engine,                // "accurate" or "light"
    Text,                  // one line of other text: a version, a hash, lengths, key names, group names
};

// Which runs write a measurement.
enum class MetricWhen {
    EveryRun,     // written at the exit of every run, on the device and off it: start_ms, exit_ms, and
                  // the keys of SPEC-241 that belong to no step of the device test and need nothing that
                  // only the device has
    WhenTaken,    // written by a run in which it was taken: a step of the device test, a set-up of HVSC,
                  // an index build, a measurement of the engines, key presses
    DeviceOnly,   // says what the device's own hardware, or HVSC itself, gave: written on the device only
};

struct MetricRow {
    const char* key;
    MetricForm form;
    MetricWhen when;
};

// Every measurement: the rows of SPEC-240's table that name one, in its order, then those of SPEC-241's.
const std::vector<MetricRow>& metric_table();
// Whether a key is one of the table's.
bool is_metric(const std::string& key);
// Whether a value has the form the table gives its key. False for a key that is not one of the table's.
bool metric_fits(const std::string& key, const std::string& value);

// The value of `user_groups`: the groups of the user the app runs as, in the order given, separated by
// single spaces, without the group that has the user's own name.
std::string user_groups_value(const std::string& user, const std::vector<std::string>& groups);

class Metrics {
public:
    // Called with each measurement as it is taken, for the self-test's results file (SPEC-260).
    using Taken = std::function<void(const std::string& key, const std::string& value)>;
    void on_taken(Taken listener);

    // Records a measurement. A key recorded again holds the newer value. False, with nothing recorded and
    // nothing told to the listener, when the key is not one of the table's or the value does not fit it.
    bool set(const std::string& key, const std::string& value);
    bool set_number(const std::string& key, std::int64_t value);

    // SPEC-112: one key event was answered, `ms` after it was read. From all of these come three
    // measurements, worked out when they are asked for: key_to_screen_ms_max, the longest; and
    // key_to_screen_ms_p95 and key_to_screen_ms_p50, the smallest number of milliseconds within which at
    // least 95, and at least 50, of every 100 were answered. With none answered, none of the three is there.
    void key_answered(Millis ms);

    bool has(const std::string& key) const;
    // "" for a key that has not been recorded.
    std::string value(const std::string& key) const;

    // One line `key = value`, ended by LF, for each measurement recorded, in the table's order, and
    // nothing else.
    std::string text() const;
    // Writes text() to a file: the file `--metrics` names.
    FileStatus write(Files* files, const std::string& path) const;

private:
    // The value of one of the three key_to_screen_ measurements; "" with no key answered. With mutex_ held.
    std::string key_figure(const std::string& key) const;
    std::string value_held(const std::string& key) const;   // with mutex_ held

    // Measurements are taken on more than one thread.
    mutable std::mutex mutex_;
    std::map<std::string, std::string> values_;
    std::map<Millis, std::uint64_t> answered_;   // how many key events were answered in each time
    std::uint64_t answered_count_ = 0;
    Taken taken_;
};

}  // namespace spz
