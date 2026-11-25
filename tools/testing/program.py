# SPDX-License-Identifier: GPL-3.0-or-later
"""Running the real program and telling it to stop (TF-052; SPEC-132, SPEC-201, SPEC-202).

The program runs with no window and on the real clock, driven by a script, in a home folder of the test's
own. The test sends it a signal at a moment it chooses and looks at how it ended.
"""

import os
import signal
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path

from .simulator import home_environment


@dataclass
class Ended:
    status: int            # the exit status; negative if a signal ended it
    seconds: float         # from the signal to the end
    output: str
    children_left: int     # processes of its group still alive afterwards


def start(program, script, home, out, extra=()):
    """Starts the program on the real clock, in its own process group, and returns the process."""
    Path(out).mkdir(parents=True, exist_ok=True)
    return subprocess.Popen([str(program), "--headless", "--clock", "real", "--script", str(script), "--out", str(out), *extra],
                            env={**os.environ, **home_environment(home)}, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, start_new_session=True)


def stop(process, how=signal.SIGTERM, wait=5.0):
    """Sends a signal and waits for the end."""
    group = os.getpgid(process.pid)
    begun = time.monotonic()
    process.send_signal(how)
    try:
        output, _ = process.communicate(timeout=wait)
    except subprocess.TimeoutExpired:
        os.killpg(group, signal.SIGKILL)
        output, _ = process.communicate()
        return Ended(-999, wait, output, 0)
    seconds = time.monotonic() - begun
    left = 0
    try:
        os.killpg(group, 0)        # is anything of the group still there?
        left = 1
        os.killpg(group, signal.SIGKILL)
    except ProcessLookupError:
        pass
    return Ended(process.returncode, seconds, output, left)
