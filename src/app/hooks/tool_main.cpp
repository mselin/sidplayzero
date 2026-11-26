// SPDX-License-Identifier: GPL-3.0-or-later
// spz_tool: the integration tests' way into the `platform` layer, one command for each real part
// (the real HttpClient, the real engine, the real unpacker, the real volume, the device's input set-up).
//
//   spz_tool COMMAND [ARGUMENT ...]
//
// Each test that uses a command says, where it uses it, what the command takes and what it prints. A
// command that is not built yet says so and ends with status 70, and the test that asked fails with
// "not built yet".
//
// Built so far:
//   spz_tool listing DATABASE [--path HVSC_PATH]      (tests/integration/test_hvsc.py, SPEC-142)
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "platform/real_files.h"
#include "spz/listing.h"

namespace {

int not_built(int argc, char** argv) {
    std::fprintf(stderr, "not built yet: spz_tool");
    for (int i = 1; i < argc; ++i) std::fprintf(stderr, " %s", argv[i]);
    std::fprintf(stderr, "\n");
    return 70;
}

// Builds the listing of a database into a folder of its own, opens it, and says what it holds.
int listing(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::fprintf(stderr, "usage: spz_tool listing DATABASE [--path HVSC_PATH]\n");
        return 2;
    }
    std::string path;
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--path" && i + 1 < args.size()) {
            path = args[++i];
        } else {
            std::fprintf(stderr, "spz_tool listing: what is %s?\n", args[i].c_str());
            return 2;
        }
    }
    char folder[] = "/tmp/spz-listing-XXXXXX";
    if (::mkdtemp(folder) == nullptr) {
        std::perror("spz_tool listing");
        return 1;
    }
    spz::platform::RealFiles files;
    const std::string file = std::string(folder) + "/listing.bin";
    const spz::ListingBuild built = spz::Listing::build(&files, args[0], file);
    spz::Listing listing;
    const bool ok = built.ok && listing.open(&files, file, args[0]);
    std::printf("ok = %s\n", ok ? "yes" : "no");
    std::printf("tunes = %d\nfolders = %d\nlengths = %d\nskipped = %d\n", built.tunes, built.folders, built.lengths,
                built.lines_skipped);
    spz::ListedTune tune;
    if (ok && !path.empty() && listing.by_path(path, &tune)) {
        std::string lengths;
        for (const spz::Millis ms : tune.lengths) lengths += (lengths.empty() ? "" : " ") + std::to_string(ms);
        std::printf("lengths_ms = %s\n", lengths.c_str());
    }
    files.remove(file);
    files.remove(folder);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return not_built(argc, argv);
    const std::string command = argv[1];
    const std::vector<std::string> args(argv + 2, argv + argc);
    if (command == "listing") return listing(args);
    return not_built(argc, argv);
}
