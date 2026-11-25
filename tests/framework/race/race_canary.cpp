// SPDX-License-Identifier: GPL-3.0-or-later
// TF-055 canary: two threads write one number with no lock [canary-fails]
//
// A program with a deliberate data race. `tools/test prove integration` runs it in the ThreadSanitizer
// build and expects the sanitizer to end it; nothing else ever runs it.
#include <cstdio>
#include <thread>

int g_number = 0;

int main() {
    std::thread a([] { for (int i = 0; i < 100000; ++i) g_number = g_number + 1; });
    std::thread b([] { for (int i = 0; i < 100000; ++i) g_number = g_number + 1; });
    a.join();
    b.join();
    std::printf("%d\n", g_number);
    return 0;
}
