// SPDX-License-Identifier: GPL-3.0-or-later
// The main() of the simulator level's C++ tests: LVGL with a display in memory, then doctest.
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "lvgl.h"
#include "platform/display.h"

int main(int argc, char** argv) {
    lv_init();
    std::string why;
    if (!spz::platform::open_display(spz::platform::DisplayKind::Memory, &why)) return 3;
    doctest::Context context(argc, argv);
    return context.run();
}
