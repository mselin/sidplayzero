# SPDX-License-Identifier: GPL-3.0-or-later
"""Fuzzing (SPEC-205, TF-060): each target is run for 15 s from its starting files, and is a fault if it
crashes, a sanitizer reports, or an input takes longer than 2 s.

A target is a file tests/fuzz/NAME_fuzz.cpp whose first lines hold "// test: SPEC-205 ...". Its starting
files are in tests/fuzz/corpus/NAME/. What a run adds to them is kept under build/, not in the repository.
"""

import re
import shutil
import subprocess
import tempfile
from pathlib import Path

from .common import (BUILD, CANARY_FAILS, FAILED, PASSED, ROOT, Result, Timer, canaries_wanted, configure_and_build,
                     needs, run, tail, Missing)

SECONDS_EACH = 15       # SPEC-205 ‡
SLOWEST_INPUT = 2       # seconds; SPEC-205 ‡
NAME_LINE = re.compile(r"(?m)^// test: (.+)$")


def targets(canaries):
    """[(program name, test name, corpus folder)]"""
    folders = [("tests/fuzz", "tests/fuzz/corpus")]
    if canaries:
        folders.append(("tests/framework/fuzz", "tests/framework/fuzz/corpus"))
    found = []
    for sources, corpus in folders:
        for path in sorted((ROOT / sources).glob("*_fuzz.cpp")):
            named = NAME_LINE.search(path.read_text(encoding="utf-8"))
            name = named.group(1).strip() if named else path.name
            found.append((path.stem, name, ROOT / corpus / path.stem[:-len("_fuzz")]))
    return found


def can_fuzz():
    """Whether clang can link a libFuzzer program here: the runtime is a package of its own."""
    with tempfile.TemporaryDirectory() as folder:
        source = Path(folder) / "probe.cpp"
        source.write_text('#include <cstddef>\n#include <cstdint>\n'
                          'extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t*, std::size_t) { return 0; }\n')
        done = subprocess.run(["clang++", "-fsanitize=fuzzer,address,undefined", str(source), "-o", str(Path(folder) / "probe")],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return done.returncode == 0


def level():
    needs("cmake", "ninja", "clang", "clang++", "python3", "git")
    if not can_fuzz():
        raise Missing(["the clang runtime with libFuzzer (Debian and Ubuntu: libclang-rt-dev)"])
    canaries = canaries_wanted()
    with Timer("fuzz targets"):
        build = configure_and_build(BUILD / "fuzz", {
            "CMAKE_C_COMPILER": "clang", "CMAKE_CXX_COMPILER": "clang++", "CMAKE_BUILD_TYPE": "RelWithDebInfo",
            "SIDPLAYZERO_BUILD_APP": "OFF", "SIDPLAYZERO_BUILD_FUZZ": "ON", "SIDPLAYZERO_SANITIZE": "fuzzer",
            "SIDPLAYZERO_FUZZ_CANARY": "ON" if canaries else "OFF"})
    results = []
    for program, name, corpus in targets(canaries):
        if CANARY_FAILS in name and not canaries:
            continue
        with Timer(f"fuzzing {program} for {SECONDS_EACH} s"):
            work = BUILD / "fuzz-out" / program
            shutil.rmtree(work, ignore_errors=True)
            (work / "corpus").mkdir(parents=True)
            command = [build / "tests" / "fuzz" / program, f"-max_total_time={SECONDS_EACH}", f"-timeout={SLOWEST_INPUT}",
                       "-rss_limit_mb=2048", f"-artifact_prefix={work}/", "-print_final_stats=1", work / "corpus"]
            if corpus.is_dir():
                command.append(corpus)
            process = run(command, timeout=SECONDS_EACH + 120)
            if process.returncode == 0:
                results.append(Result(name, PASSED))
            else:
                why = next((line.strip() for line in process.stdout.split("\n")
                            if "ERROR:" in line or "SUMMARY:" in line or "runtime error" in line), "")
                results.append(Result(name, FAILED, why or tail(process.stdout, 8)))
    return results
