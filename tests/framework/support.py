# SPDX-License-Identifier: GPL-3.0-or-later
"""Helpers for the framework's own tests."""

import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))


def run(command, env=None, cwd=None, timeout=600):
    merged = dict(os.environ)
    merged.pop("SIDPLAYZERO_CANARIES", None)   # a test of the framework never runs the failing canaries
    if env:
        merged.update(env)
    return subprocess.run([str(c) for c in command], cwd=cwd or ROOT, env=merged, text=True, timeout=timeout,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, errors="replace")


def unit_build():
    """The plain unit build's folder, which tools/test unit has made before these tests run."""
    return Path(os.environ.get("SPZ_UNIT_BUILD", ROOT / "build" / "unit"))
