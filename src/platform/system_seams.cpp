// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/system_seams.h"

#include <dirent.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

#include "platform/child_process.h"
#include "spz/complete_download.h"

namespace spz::platform {

namespace {

// Programs are asked to speak plainly, whatever the user's language: their words are read here.
const std::vector<std::string> kPlainLanguage = {"LC_ALL=C.UTF-8", "LANG=C.UTF-8", "LANGUAGE="};

// SPEC-204: the listing that 7z prints of an archive is read up to 64 MiB.
const std::size_t kListingLimitBytes = 64u * 1024u * 1024u;
// SPEC-149: 7z runs with a time limit. Listing reads only the archive's headers; unpacking HVSC on the
// device takes minutes, and is given an hour.
const long kListTimeoutMs = 5L * 60L * 1000L;
const long kUnpackTimeoutMs = 60L * 60L * 1000L;

bool read_number_file(const std::string& path, long* out) {
    std::ifstream file(path);
    long value = 0;
    if (!(file >> value)) return false;
    *out = value;
    return true;
}

}  // namespace

// --- 7z ---------------------------------------------------------------------------------------------------

std::string SevenZipUnpacker::program() {
    for (const char* name : {"7z", "7zz", "7za"}) {
        const std::string found = find_program(name);
        if (!found.empty()) return found;
    }
    return "";
}

bool SevenZipUnpacker::available() { return !program().empty(); }

bool SevenZipUnpacker::list(const std::string& archive, std::vector<ArchiveEntry>* out) {
    const std::string seven = program();
    if (seven.empty()) return false;
    // What 7z prints is kept up to SPEC-204's limit; a longer listing is refused. Once it has named more
    // entries than SPEC-149 allows, the rest is not needed: 7z is stopped, and what was read is enough
    // for the check to refuse the archive.
    std::string listing;
    std::uint64_t paths = 0;        // lines that begin "Path = ": the archive's own, then one for each entry
    std::size_t scanned = 0;        // where to look for the next of them
    bool too_long = false;
    bool too_many = false;
    const std::string path_line = "\nPath = ";
    ChildOptions options;
    options.environment = kPlainLanguage;
    options.own_group = true;
    options.timeout_ms = kListTimeoutMs;
    options.output = [&](const std::string& piece) {
        if (piece.size() > kListingLimitBytes - listing.size()) {
            too_long = true;
            return false;
        }
        listing += piece;
        for (std::size_t at = listing.find(path_line, scanned); at != std::string::npos;
             at = listing.find(path_line, at + 1)) {
            ++paths;
        }
        scanned = listing.size() >= path_line.size() ? listing.size() - path_line.size() + 1 : 0;
        // The archive's own Path, kArchiveMaxEntries + 1 entries, and the Path that ends the last of them.
        if (paths >= kArchiveMaxEntries + 3) {
            too_many = true;
            return false;
        }
        return true;
    };
    const ChildResult ran = run_child(seven, {"7z", "l", "-slt", "-t7z", "--", archive}, options);
    if (too_long || (!too_many && (!ran.exited || ran.status != 0 || ran.stopped))) return false;
    std::vector<ArchiveEntry> entries;
    if (!read_7z_listing(listing, &entries)) return false;
    if (too_many && entries.size() <= kArchiveMaxEntries) return false;
    *out = std::move(entries);
    return true;
}

bool SevenZipUnpacker::unpack(const std::string& archive, const std::string& into, const Progress& progress) {
    const std::string seven = program();
    if (seven.empty()) return false;
    std::uint64_t files = 0;
    std::string line;
    bool stop = false;
    ChildOptions options;
    options.environment = kPlainLanguage;
    options.own_group = true;
    options.timeout_ms = kUnpackTimeoutMs;
    options.output = [&](const std::string& piece) {
        // With -bb1, 7z names each file and folder it writes on a line of its own, after "- ".
        for (char c : piece) {
            if (c != '\n') {
                if (line.size() < 65536) line += c;   // only its beginning is looked at
                continue;
            }
            if (line.compare(0, 2, "- ") == 0 && line.back() != '/') {   // a folder's line ends in "/"
                ++files;
                if (progress && !progress(files)) stop = true;
            }
            line.clear();
        }
        return !stop;
    };
    const ChildResult ran =
        run_child(seven, {"7z", "x", "-t7z", "-mmt1", "-y", "-bd", "-bb1", "-o" + into, "--", archive}, options);
    return ran.exited && ran.status == 0 && !ran.stopped && !stop;
}

// --- pactl ------------------------------------------------------------------------------------------------

namespace {

bool pactl(const std::vector<std::string>& arguments, std::string* said) {
    const std::string program = find_program("pactl");
    if (program.empty()) return false;
    std::vector<std::string> argv = {"pactl"};
    argv.insert(argv.end(), arguments.begin(), arguments.end());
    ChildOptions options;
    options.environment = kPlainLanguage;
    options.timeout_ms = 2000;
    const ChildResult ran = run_child(program, argv, options);
    if (!ran.exited || ran.status != 0 || ran.stopped) return false;
    if (said) *said = ran.output;
    return true;
}

}  // namespace

bool PactlVolume::get(int* percent, bool* muted) {
    std::string volume, mute;
    if (!pactl({"get-sink-volume", "@DEFAULT_SINK@"}, &volume)) return false;
    if (!pactl({"get-sink-mute", "@DEFAULT_SINK@"}, &mute)) return false;
    // "Volume: front-left: 27525 /  42% / -22.61 dB, ...": the first number before a "%".
    const std::size_t sign = volume.find('%');
    if (sign == std::string::npos) return false;
    std::size_t start = sign;
    while (start > 0 && volume[start - 1] >= '0' && volume[start - 1] <= '9') --start;
    if (start == sign) return false;
    const int level = std::stoi(volume.substr(start, sign - start));
    // "Mute: yes" or "Mute: no".
    const std::size_t word = mute.find("Mute:");
    if (word == std::string::npos) return false;
    std::istringstream rest(mute.substr(word + 5));
    std::string answer;
    rest >> answer;
    if (answer != "yes" && answer != "no") return false;
    if (percent) *percent = level;
    if (muted) *muted = answer == "yes";
    return true;
}

bool PactlVolume::set(int percent) {
    percent = std::max(0, std::min(100, percent));
    return pactl({"set-sink-volume", "@DEFAULT_SINK@", std::to_string(percent) + "%"}, nullptr);
}

bool PactlVolume::toggle_mute() { return pactl({"set-sink-mute", "@DEFAULT_SINK@", "toggle"}, nullptr); }

// --- the backlight ----------------------------------------------------------------------------------------

SysfsBacklight::SysfsBacklight(std::string folder) : folder_(std::move(folder)) {}

bool SysfsBacklight::find() {
    if (!device_.empty()) return true;
    DIR* folder = ::opendir(folder_.c_str());
    if (folder == nullptr) return false;
    std::vector<std::string> names;
    while (const dirent* entry = ::readdir(folder)) {
        if (entry->d_name[0] != '.') names.push_back(entry->d_name);
    }
    ::closedir(folder);
    std::sort(names.begin(), names.end());
    for (const std::string& name : names) {
        const std::string device = folder_ + "/" + name;
        long most = 0;
        if (::access((device + "/brightness").c_str(), W_OK) == 0 &&
            read_number_file(device + "/max_brightness", &most) && most > 0) {
            device_ = device;
            return true;
        }
    }
    return false;
}

bool SysfsBacklight::can_change() { return find(); }

bool SysfsBacklight::get(int* percent) {
    long most = 0, now = 0;
    if (!find() || !read_number_file(device_ + "/max_brightness", &most) || most <= 0 ||
        !read_number_file(device_ + "/brightness", &now)) {
        return false;
    }
    if (percent) *percent = static_cast<int>(std::lround(100.0 * static_cast<double>(now) / static_cast<double>(most)));
    return true;
}

bool SysfsBacklight::set(int percent) {
    long most = 0;
    if (!find() || !read_number_file(device_ + "/max_brightness", &most) || most <= 0) return false;
    percent = std::max(0, std::min(100, percent));
    const long level = std::lround(static_cast<double>(most) * percent / 100.0);
    std::FILE* file = std::fopen((device_ + "/brightness").c_str(), "w");
    if (file == nullptr) return false;
    const bool written = std::fprintf(file, "%ld\n", level) > 0;
    return std::fclose(file) == 0 && written;
}

// --- memory -----------------------------------------------------------------------------------------------

namespace {

std::uint64_t meminfo_mib(const char* key) {
    std::ifstream file("/proc/meminfo");
    std::string name;
    std::uint64_t kib = 0;
    std::string unit;
    while (file >> name >> kib) {
        std::getline(file, unit);
        if (name == key) return kib / 1024;
    }
    return 0;
}

}  // namespace

std::uint64_t ProcMemory::available_mib() { return meminfo_mib("MemAvailable:"); }
std::uint64_t ProcMemory::swap_free_mib() { return meminfo_mib("SwapFree:"); }

}  // namespace spz::platform
