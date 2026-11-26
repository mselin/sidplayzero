// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/hooks/sim_fakes.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace spz::hooks {

namespace {

std::string trim(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos) return "";
    return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

bool truth(const std::string& value) { return value == "yes" || value == "true" || value == "on" || value == "1"; }

std::vector<std::string> split(const std::string& line, char separator) {
    std::vector<std::string> cells;
    std::string cell;
    std::istringstream in(line);
    while (std::getline(in, cell, separator)) cells.push_back(cell);
    return cells;
}

}  // namespace

std::string SimFakes::resolve(const std::string& name) const {
    if (name.compare(0, 9, "fixtures/") == 0) {
        const char* fixtures = std::getenv("SPZ_FIXTURES");
        return std::string(fixtures ? fixtures : "tests/fixtures") + name.substr(8);
    }
    return folder.empty() ? name : folder + "/" + name;
}

bool SimFakes::set(const std::string& name, const std::string& value, std::string* why) {
    try {
        if (name == "memory.available_mib") memory.available = std::stoull(value);
        else if (name == "memory.swap_free_mib") memory.swap_free = std::stoull(value);
        else if (name == "files.free_mib") files.set_free_bytes(std::stoull(value) * 1024 * 1024);
        else if (name == "files.list_delay_ms") list_delay_ms = std::stoll(value);
        else if (name == "files.root_writable") { if (!truth(value)) files.make_read_only(value); }
        else if (name == "volume.level") volume.level = std::stoi(value);
        else if (name == "volume.works") volume.works = truth(value);
        else if (name == "backlight.level") backlight.level = std::stoi(value);
        else if (name == "backlight.writable") backlight.writable = truth(value);
        else if (name == "unpacker.available") unpacker.is_available = truth(value);
        else if (name == "unpacker.tree") unpacker.tree = folder.empty() ? value : folder + "/" + value;
        else if (name == "engine.speed") engine.frames_per_call = static_cast<std::size_t>(std::stoull(value));
        else { *why = "there is no fake value named " + name; return false; }
    } catch (const std::exception&) {
        *why = "'" + value + "' is not a value for " + name;
        return false;
    }
    return true;
}

bool SimFakes::load(const std::string& from, const std::string&, std::string* why) {
    folder = from;
    http.forbidden = true;   // without http.tsv, any request fails the run
    if (folder.empty()) return true;

    std::ifstream ini(folder + "/fakes.ini");
    std::string line;
    int number = 0;
    while (std::getline(ini, line)) {
        ++number;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos || !set(trim(line.substr(0, equals)), trim(line.substr(equals + 1)), why)) {
            *why = "fakes.ini line " + std::to_string(number) + ": " + (why->empty() ? "not name = value" : *why);
            return false;
        }
    }

    std::ifstream table(folder + "/http.tsv");
    if (!table) return true;
    http.forbidden = false;
    number = 0;
    while (std::getline(table, line)) {
        ++number;
        if (trim(line).empty() || line[0] == '#') continue;
        const std::vector<std::string> cells = split(line, '\t');
        if (cells.size() < 3) { *why = "http.tsv line " + std::to_string(number) + ": needs a URL, a status and a file"; return false; }
        fake::HttpClient::Answer answer;
        try {
            answer.status = std::stoi(cells[1]);
        } catch (const std::exception&) {
            *why = "http.tsv line " + std::to_string(number) + ": the status is not a number";
            return false;
        }
        if (cells[2] != "-") {
            std::ifstream body(resolve(cells[2]), std::ios::binary);
            if (!body) { *why = "http.tsv line " + std::to_string(number) + ": no file " + cells[2]; return false; }
            answer.body.assign(std::istreambuf_iterator<char>(body), std::istreambuf_iterator<char>());
        }
        std::istringstream options(cells.size() > 3 ? cells[3] : "");
        std::string option;
        while (options >> option) {
            if (option == "stall") answer.stall = true;
            else if (option.compare(0, 4, "cut=") == 0) answer.cut_after = std::stoull(option.substr(4));
            else if (option.compare(0, 5, "etag=") == 0) answer.etag = option.substr(5);
            else if (option == "error=connect" || option == "error=timeout") answer.failure = HttpFailure::Network;
            else if (option == "error=tls") answer.failure = HttpFailure::Tls;
            else { *why = "http.tsv line " + std::to_string(number) + ": unknown option " + option; return false; }
        }
        http.answer(cells[0], answer);
    }
    return true;
}

bool SimFakes::log(const std::string& name, std::string* text) const {
    const std::vector<std::string>* lines = nullptr;
    if (name == "volume") lines = &volume.log;
    else if (name == "backlight") lines = &backlight.log;
    else if (name == "unpacker") lines = &unpacker.log;
    else return false;
    text->clear();
    for (const std::string& line : *lines) *text += line + "\n";
    return true;
}

}  // namespace spz::hooks
