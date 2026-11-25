// SPDX-License-Identifier: GPL-3.0-or-later
// STUB. spz_tool: the integration tests' way into the `platform` layer, one command for each real part
// (the real HttpClient, the real engine, the real unpacker, the real volume, the device's input set-up).
//
//   spz_tool COMMAND [ARGUMENT ...]
//
// Each test that uses a command says, where it uses it, what the command takes and what it prints. Before
// Phase 5 there is no command at all: whatever is asked, the tool says so and ends with status 70, and
// the test that asked fails with "not built yet".
#include <cstdio>

int main(int argc, char** argv) {
    std::fprintf(stderr, "not built yet: spz_tool");
    for (int i = 1; i < argc; ++i) std::fprintf(stderr, " %s", argv[i]);
    std::fprintf(stderr, "\n");
    return 70;
}
