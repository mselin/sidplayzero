// SPDX-License-Identifier: GPL-3.0-or-later
// What the unit tests share: a folder of a test's own, and the fixtures of tools/make_fixtures.py.
#pragma once

#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "spz/types.h"

namespace spz::test {

// A new, empty folder for one test, removed afterwards.
struct TempFolder {
    std::filesystem::path path;
    TempFolder() {
        std::string name = (std::filesystem::temp_directory_path() / "spz-test-XXXXXX").string();
        if (mkdtemp(name.data()) == nullptr) throw std::runtime_error("no temporary folder");
        path = name;
    }
    ~TempFolder() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;

    // The full path of a name inside the folder.
    std::string file(const std::string& name) const { return (path / name).string(); }
    // Writes a file inside the folder, making its parent folders.
    std::string write(const std::string& name, const spz::Bytes& data) const {
        const std::filesystem::path target = path / name;
        std::filesystem::create_directories(target.parent_path());
        std::ofstream out(target, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        return target.string();
    }
    std::string write(const std::string& name, const std::string& text) const {
        return write(name, spz::Bytes(text.begin(), text.end()));
    }
};

inline spz::Bytes bytes(const std::string& text) { return spz::Bytes(text.begin(), text.end()); }

// The path of a fixture, such as "tunes/commando_test.sid" (tests/fixtures/, from tools/make_fixtures.py).
inline std::string fixture_path(const std::string& name) { return std::string(SPZ_FIXTURES) + "/" + name; }

// A fixture's bytes. Throws if it is not there: the fixtures are made before the tests run.
inline spz::Bytes fixture(const std::string& name) {
    std::ifstream in(fixture_path(name), std::ios::binary);
    if (!in) throw std::runtime_error("no fixture " + name + ": run tools/make_fixtures.py");
    return spz::Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Copies a fixture folder, such as "collections/85/C64Music", to a place of the test's own.
inline void copy_fixture_tree(const std::string& name, const std::filesystem::path& to) {
    std::filesystem::create_directories(to.parent_path());
    std::filesystem::copy(fixture_path(name), to, std::filesystem::copy_options::recursive);
}

}  // namespace spz::test
