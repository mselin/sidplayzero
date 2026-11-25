# SPDX-License-Identifier: GPL-3.0-or-later
"""The device level (SPEC-260; TF-080, TF-081): nothing of it can run here. `tools/test device` builds the
package and says what to copy to the device and what to run there, with the tester present."""

from . import deb
from .common import ROOT, Timer, needs, say


def level():
    needs("cmake", "ninja", "aarch64-linux-gnu-gcc", "aarch64-linux-gnu-g++", "dpkg-deb")
    with Timer("the package: cmake --workflow --preset cp0-cross-package"):
        package = deb.build()
    say("")
    say("The device level runs on the device, with its tester present. Copy these two files to it:")
    say(f"    {package.relative_to(ROOT)}")
    say("    tools/device-run.sh")
    say("Then, over SSH, as the device's normal user:")
    say(f"    ./device-run.sh --prove {package.name}     the device level's two canaries, about two minutes")
    say(f"    ./device-run.sh {package.name}             the whole sitting of docs/device-checklist.md, from Phase 5 on")
    say("Each writes a results file beside it. Read it, then send it back.")
    say("")
    return []
