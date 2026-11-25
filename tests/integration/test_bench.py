# SPDX-License-Identifier: GPL-3.0-or-later
"""SPEC-262: the engine benchmark, built for this machine."""

import re
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class Benchmark(unittest.TestCase):
    def test_host_bench_prints_twelve_factors(self):
        """SPEC-262 the bench program, built for the host, prints twelve speed factors greater than 0 in the stated form"""
        built = subprocess.run([str(ROOT / "tools/engine-bench/build.sh"), "--host"], text=True, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT)
        self.assertEqual(built.returncode, 0, built.stdout[-800:])
        ran = subprocess.run([str(ROOT / "tools/engine-bench/dist/sidplayzero-bench-host")], text=True,
                             stdout=subprocess.PIPE, timeout=900)
        self.assertEqual(ran.returncode, 0, ran.stdout)
        factors = re.findall(r"(?m)^engine_bench_(accurate|light)_([123])sid_(44100|48000) = (\d+\.\d+)$", ran.stdout)
        self.assertEqual(len({f[:3] for f in factors}), 12, ran.stdout)
        self.assertTrue(all(float(f[3]) > 0 for f in factors))
        self.assertRegex(ran.stdout, r"(?m)^engine_bench_library = libsidplayfp 3\.1\.2$")
        self.assertRegex(ran.stdout, r"(?m)^engine_bench_cpu = \S")
