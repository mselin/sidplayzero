# SPDX-License-Identifier: GPL-3.0-or-later
"""SPEC-206: an ordinary user's program. What the package holds is checked in test_package.py (no maintainer
scripts, no setuid or setgid file, no service); this is the check of the source: the app never asks for
more rights."""

import re
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# What becomes the program, and what is installed with it.
SOURCE_FOLDERS = ("src", "packaging", "cmake")
# A command that runs something as another user, as a word of its own: in a string, an argument list, a script.
ASKS_FOR_RIGHTS = re.compile(r"(?<![\w./-])(sudo|su|pkexec)(?![\w.-])")


def source_files():
    listed = subprocess.run(["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard", *SOURCE_FOLDERS],
                            cwd=ROOT, text=True, stdout=subprocess.PIPE, check=True).stdout.split("\0")
    return [name for name in listed if name and (ROOT / name).is_file()]


class OrdinaryUser(unittest.TestCase):
    def test_source_runs_no_sudo(self):
        """SPEC-206 the source of the program and of its package runs no sudo, su or pkexec"""
        files = source_files()
        self.assertGreater(len(files), 20, "the source was not found")
        found = []
        for name in files:
            data = (ROOT / name).read_bytes()
            if b"\0" in data[:4096]:
                continue        # a picture, not source
            for number, line in enumerate(data.decode("utf-8", errors="replace").split("\n"), 1):
                if ASKS_FOR_RIGHTS.search(line):
                    found.append(f"{name}:{number}: {line.strip()[:100]}")
        self.assertEqual(found, [])

    def test_the_check_notices(self):
        """SPEC-206 the same check does notice a program started through sudo, su or pkexec"""
        for line in ('execlp("sudo", "sudo", "tee", path, nullptr);', "system(\"su -c 'echo 50 > brightness'\");",
                     'const char* argv[] = {"pkexec", "sh", nullptr};', "sudo systemctl restart sidplayzero"):
            self.assertIsNotNone(ASKS_FOR_RIGHTS.search(line), line)
        for line in ("// the result of the sum", "std::string suffix;", "bool issue = false;", "#include <unistd.h>",
                     "cmake --preset cp0-cross"):
            self.assertIsNone(ASKS_FOR_RIGHTS.search(line), line)


if __name__ == "__main__":
    unittest.main()
