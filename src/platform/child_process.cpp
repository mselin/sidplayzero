// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/child_process.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>

extern char** environ;

namespace spz::platform {

namespace {

std::mutex g_groups_mutex;
std::set<pid_t> g_groups;

long now_ms() {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<long>(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
}

bool is_program(const std::string& path) {
    struct stat status {};
    return ::stat(path.c_str(), &status) == 0 && S_ISREG(status.st_mode) && ::access(path.c_str(), X_OK) == 0;
}

}  // namespace

std::string find_program(const std::string& name) {
    if (name.empty() || name.find('/') != std::string::npos) return "";
    const char* path = std::getenv("PATH");
    if (path == nullptr) return "";
    std::string all = path;
    std::size_t start = 0;
    while (start <= all.size()) {
        const std::size_t end = all.find(':', start);
        const std::string folder = all.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!folder.empty()) {
            const std::string candidate = folder + "/" + name;
            if (is_program(candidate)) return candidate;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return "";
}

ChildResult run_child(const std::string& program, const std::vector<std::string>& arguments,
                      const ChildOptions& options) {
    ChildResult result;
    int pipe_fds[2];
    if (::pipe2(pipe_fds, O_CLOEXEC) != 0) return result;

    // The environment: this process's, with the options' variables in place of any of the same name.
    std::vector<std::string> environment;
    for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
        const std::string text = *entry;
        const std::string name = text.substr(0, text.find('='));
        bool replaced = false;
        for (const std::string& wanted : options.environment) {
            if (wanted.compare(0, name.size() + 1, name + "=") == 0) replaced = true;
        }
        if (!replaced) environment.push_back(text);
    }
    for (const std::string& wanted : options.environment) environment.push_back(wanted);
    std::vector<char*> envp;
    for (std::string& text : environment) envp.push_back(&text[0]);
    envp.push_back(nullptr);
    std::vector<std::string> copies = arguments;
    if (copies.empty()) copies.push_back(program);
    std::vector<char*> argv;
    for (std::string& text : copies) argv.push_back(&text[0]);
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    short flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
    sigset_t none, defaults;
    sigemptyset(&none);
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGPIPE);
    sigaddset(&defaults, SIGTERM);
    sigaddset(&defaults, SIGINT);
    sigaddset(&defaults, SIGCHLD);
    posix_spawnattr_setsigmask(&attributes, &none);
    posix_spawnattr_setsigdefault(&attributes, &defaults);
    if (options.own_group) {
        flags |= POSIX_SPAWN_SETPGROUP;
        posix_spawnattr_setpgroup(&attributes, 0);
    }
    posix_spawnattr_setflags(&attributes, flags);

    pid_t pid = -1;
    const int spawned = ::posix_spawn(&pid, program.c_str(), &actions, &attributes, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    ::close(pipe_fds[1]);
    if (spawned != 0) {
        ::close(pipe_fds[0]);
        return result;
    }
    result.started = true;
    if (options.own_group) {
        std::lock_guard<std::mutex> lock(g_groups_mutex);
        g_groups.insert(pid);
    }
    auto kill_it = [&] {
        if (options.own_group) {
            ::kill(-pid, SIGKILL);
        } else {
            ::kill(pid, SIGKILL);
        }
        result.stopped = true;
    };

    const long started = now_ms();
    char buffer[16384];
    bool open = true;
    while (open) {
        pollfd waiting{pipe_fds[0], POLLIN, 0};
        const int ready = ::poll(&waiting, 1, 100);
        if (ready < 0 && errno != EINTR) break;
        if (ready > 0) {
            const ssize_t got = ::read(pipe_fds[0], buffer, sizeof buffer);
            if (got < 0 && (errno == EINTR || errno == EAGAIN)) continue;
            if (got <= 0) {
                open = false;
            } else if (options.output) {
                if (!result.stopped && !options.output(std::string(buffer, static_cast<std::size_t>(got)))) kill_it();
            } else if (!result.stopped) {
                const std::size_t piece = static_cast<std::size_t>(got);
                if (piece > options.output_limit - std::min(options.output_limit, result.output.size())) {
                    kill_it();   // more than it is allowed to say
                } else {
                    result.output.append(buffer, piece);
                }
            }
        }
        if (!result.stopped && options.timeout_ms > 0 && now_ms() - started > options.timeout_ms) kill_it();
    }
    ::close(pipe_fds[0]);
    if (options.own_group) {
        // Whatever it left behind in its group is killed once it has ended, but before it is reaped: until
        // then its process id, and so the group's, cannot be given to another process.
        siginfo_t ended{};
        int waiting;
        do {
            waiting = ::waitid(P_PID, static_cast<id_t>(pid), &ended, WEXITED | WNOWAIT);
        } while (waiting < 0 && errno == EINTR);
        ::kill(-pid, SIGKILL);
        std::lock_guard<std::mutex> lock(g_groups_mutex);
        g_groups.erase(pid);   // before the id can be reused, so that stop_all_children() cannot hit another
    }
    int status = 0;
    pid_t waited;
    do {
        waited = ::waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited == pid && WIFEXITED(status)) {
        result.exited = true;
        result.status = WEXITSTATUS(status);
    }
    return result;
}

void stop_all_children() {
    std::lock_guard<std::mutex> lock(g_groups_mutex);
    for (const pid_t group : g_groups) ::kill(-group, SIGKILL);
}

}  // namespace spz::platform
