// SPDX-License-Identifier: GPL-3.0-or-later
// Private to spz_tool (tool_main.cpp, tool_hvsc.cpp, tool_sound.cpp): what its commands share, and the
// commands, each `int NAME(args)` that returns the tool's exit status. What the tool is and what each command
// prints is at the top of tool_main.cpp.
#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "app/workers.h"
#include "platform/curl_http.h"
#include "platform/real_clock.h"
#include "platform/real_files.h"
#include "spz/job.h"
#include "spz/notice_gate.h"

namespace spz::tool {

using platform::RealFiles;

// Says how a command is used, on stderr, and gives the status of a usage error (2).
int usage(const char* text);
// Prints `key = value` on stdout, at once.
void say(const std::string& key, const std::string& value);

// Options of the form --name VALUE, and a flag or two; the rest are the command's own words, in order.
struct Arguments {
    std::vector<std::string> words;
    std::map<std::string, std::string> values;
    std::vector<std::string> flags;

    bool flag(const std::string& name) const;
    std::string value(const std::string& name, const std::string& otherwise = "") const;
};
// False, having said why, if an option wants a value and has none.
bool parse(const std::vector<std::string>& args, const std::vector<std::string>& flag_names, Arguments* out);

// The rules of the real HttpClient a command asked for: the release's, or a test's local server (with
// --ca FILE as the authority it trusts).
platform::HttpRules rules_from(const Arguments& a);

// What the jobs of `core` need around them.
class AcceptedNotice : public NoticeRecord {
public:
    bool notice_accepted() const override { return true; }
    void accept_notice() override {}
};
class NoPanel : public NoticePanel {
public:
    void open_notice_panel(const std::string&) override {}
};
class PrintedNotices : public NoticeSink {   // each notice as `notice = ID key=value ...`
public:
    void raise(const std::string& id, const std::map<std::string, std::string>& about) override;
};

// The UI thread of the tool: the main thread, running what is posted to it and the clock's timers until
// `done` says so, or the time is up.
bool run_ui(app::UiQueue* ui, platform::RealClock* clock, const std::function<bool()>& done,
            const std::function<void()>& each = nullptr, long seconds = 3600);

// The commands.
int listing(const std::vector<std::string>& args);             // tool_main.cpp
int fetch(const std::vector<std::string>& args);
int unpacker(const std::vector<std::string>& args);
int volume(const std::vector<std::string>& args);
int input_setup(const std::vector<std::string>& args);
int kept_files(const std::vector<std::string>& args);
int setup(const std::vector<std::string>& args);               // tool_hvsc.cpp
int complete_download(const std::vector<std::string>& args);
int audio_guard(const std::vector<std::string>& args);         // tool_sound.cpp
int load_tunes(const std::vector<std::string>& args);

}  // namespace spz::tool
