// SPDX-License-Identifier: GPL-3.0-or-later
// STUB (TF-005): see stub_app.h.
#include "app/stub_app.h"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "spz/keys.h"

namespace spz::app {

namespace {

const std::int64_t kHoldNoticeMs = 500;    // SPEC-106
const std::int64_t kHoldLeaveMs = 1000;

std::string home() {
    const char* value = std::getenv("HOME");
    return value && *value ? value : "/";
}

}  // namespace

std::string collection_root() {
    const char* base = std::getenv("HVSC_BASE");
    if (base && *base) return base;
    return home() + "/C64Music";
}

std::string state_folder() {
    const char* state = std::getenv("XDG_STATE_HOME");
    if (state && *state) return std::string(state) + "/sidplayzero";
    return home() + "/.local/state/sidplayzero";
}

Lock::~Lock() { release(); }

bool Lock::take() {
    std::error_code ec;
    std::filesystem::create_directories(state_folder(), ec);
    fd_ = ::open((state_folder() + "/lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd_ < 0) return true;   // a state folder that cannot be written is no reason not to start
    if (::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    return true;
}

void Lock::release() {
    if (fd_ >= 0) {
        ::flock(fd_, LOCK_UN);
        ::close(fd_);
        fd_ = -1;
    }
}

long long ms_since_process_start() {
    std::ifstream stat("/proc/self/stat");
    std::string line;
    std::getline(stat, line);
    const std::size_t after_name = line.rfind(')');     // the program's name may hold spaces
    if (after_name == std::string::npos) return -1;
    std::istringstream fields(line.substr(after_name + 2));
    std::string field;
    for (int i = 3; i <= 22 && (fields >> field); ++i) {}   // field 22 is the start time, in clock ticks
    std::ifstream uptime("/proc/uptime");
    double up = 0;
    uptime >> up;
    const long ticks = sysconf(_SC_CLK_TCK);
    if (field.empty() || ticks <= 0 || up <= 0) return -1;
    const double started = std::stod(field) / static_cast<double>(ticks);
    return static_cast<long long>((up - started) * 1000.0);
}

void record_for_selftest(const std::string& key, const std::string& value) {
    const std::string folder = state_folder();
    if (!std::filesystem::exists(folder + "/selftest.request")) return;
    std::ofstream results(folder + "/selftest.results", std::ios::app);
    results << key << " = " << value << "\n";
}

StubApp::StubApp(Clock* clock) : clock_(clock), screen_(new ui::StubScreen(collection_root())) {}

StubApp::~StubApp() {
    clock_->cancel(notice_timer_);
    clock_->cancel(leave_timer_);
}

void StubApp::key(int linux_code, bool pressed) {
    if (key_from_code(linux_code) != Key::Esc) return;   // the stub has no other key
    if (pressed && !esc_down_) {
        esc_down_ = true;
        notice_timer_ = clock_->call_after(kHoldNoticeMs, [this] { screen_->show_hold_notice(true); });
        leave_timer_ = clock_->call_after(kHoldLeaveMs, [this] { leaving_ = true; });
    } else if (!pressed && esc_down_) {
        esc_down_ = false;
        clock_->cancel(notice_timer_);
        clock_->cancel(leave_timer_);
        screen_->show_hold_notice(false);
    }
}

}  // namespace spz::app
