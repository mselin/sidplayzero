// SPDX-License-Identifier: GPL-3.0-or-later
// See start.h.
#include "app/start.h"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "platform/real_files.h"
#include "spz/folders.h"

namespace spz::app {

namespace {

std::string variable(const char* name) {
    const char* value = std::getenv(name);
    return value ? value : "";
}

// The lock's path: in SPEC-134's state folder, worked out from the environment as the composition works
// it out for state.ini (src/app/compose_keep.cpp).
std::string lock_file() {
    Environment environment;
    environment.home = variable("HOME");
    environment.xdg_config_home = variable("XDG_CONFIG_HOME");
    environment.xdg_data_home = variable("XDG_DATA_HOME");
    environment.xdg_state_home = variable("XDG_STATE_HOME");
    environment.xdg_cache_home = variable("XDG_CACHE_HOME");
    return lock_path(app_folders(environment));
}

}  // namespace

Lock::~Lock() { release(); }

bool Lock::take() {
    const std::string path = lock_file();
    platform::RealFiles files;
    files.make_folders(path.substr(0, path.rfind('/')));   // for the user only (SPEC-134)
    fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
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

}  // namespace spz::app
