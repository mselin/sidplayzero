# SPDX-License-Identifier: GPL-3.0-or-later
"""The host-integration level (TF-050 to TF-056): real parts on this machine.

The pinned engine and reference program; the real program started, signalled and killed; a local HTTPS
server; a real 7z and stand-ins that record their arguments; and the simulator's tests once more in a
build with ThreadSanitizer.
"""

import sys

from . import engine, simulator
from .common import (BUILD, CANARY_FAILS, FAILED, PASSED, ROOT, Result, Timer, canaries_wanted, configure_and_build,
                     needs, run, tail)
from .unit import python_results

RACE_CANARY = "TF-055 canary: two threads write one number with no lock [canary-fails]"


def level():
    needs("cmake", "ninja", "c++", "cc", "python3", "git", "make", "pkg-config", "curl", "openssl", "sdl2-config")
    canaries = canaries_wanted()
    results = []
    with Timer("fixtures (tools/make_fixtures.py)"):
        run([sys.executable, ROOT / "tools" / "make_fixtures.py"], check=True)
    with Timer("the engine and the reference program, from their pinned archives"):
        engine.build()
    with Timer("the program, for the tests that start and stop it"):
        build = configure_and_build(BUILD / "simulator", {
            "SIDPLAYZERO_TEST_HOOKS": "ON", "SIDPLAYZERO_DISPLAY": "window", "SIDPLAYZERO_BUILD_SIM_TESTS": "ON"})
    with Timer("the tool for the tests of real parts, under AddressSanitizer"):
        asan = configure_and_build(BUILD / "tool-asan", {
            "SIDPLAYZERO_TEST_HOOKS": "ON", "SIDPLAYZERO_DISPLAY": "none", "SIDPLAYZERO_SANITIZE": "address",
            "CMAKE_BUILD_TYPE": "Debug", "SIDPLAYZERO_BUILD_UNIT_TESTS": "OFF"}, targets=["spz_tool"])
    with Timer("integration tests"):
        env = {"SPZ_SIM_BUILD": str(build), "SPZ_ASAN_TOOL": str(asan / "src" / "spz_tool")}
        results += python_results("tests/framework/integration", canaries, env)
        results += python_results("tests/integration", canaries, env)
    with Timer("the simulator's tests under ThreadSanitizer"):
        tsan = [("simulator-tsan", "thread sanitizer", {"SIDPLAYZERO_SANITIZE": "thread", "CMAKE_BUILD_TYPE": "Debug",
                                                        "SIDPLAYZERO_BUILD_RACE_CANARY": "ON"})]
        results += [r for r in simulator.level(variants=tsan, run_python=False) if CANARY_FAILS not in r.name]
        if canaries:
            process = run([BUILD / "simulator-tsan" / "tests" / "simulator" / "spz_race_canary"], timeout=120)
            caught = process.returncode != 0 and "ThreadSanitizer: data race" in process.stdout
            results.append(Result(RACE_CANARY, FAILED if caught else PASSED,
                                  "ThreadSanitizer: data race" if caught else tail(process.stdout, 5), "thread sanitizer"))
    return results
