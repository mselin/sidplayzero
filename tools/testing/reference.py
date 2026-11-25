# SPDX-License-Identifier: GPL-3.0-or-later
"""Reference renders (TF-051; SPEC-071): what the pinned sidplayfp program makes of a tune, as samples.

Nothing is compared with stored sample values: a reference is made when a test runs, with the command the
specification gives, in an empty home folder so that no settings file of the user's can change it.
"""

import tempfile
import wave
from pathlib import Path

from . import engine
from .common import run


def render(tune, seconds=10, accurate=True, rate=48000, subtune=None, extra=()):
    """The samples of the first `seconds` of a tune, as the bytes of 16-bit stereo frames.

    The command is SPEC-071's: sidplayfp --delay=0 -s -rr -f48000 -t10 --residfp -wOUT.wav TUNE.sid, with
    --sidlite for the light engine, another -f for the other rate, -oN for subtune N, and what a test adds
    (-u1 to mute a voice, -nf for no filter, -mnf and -vnf to force the chip and the clock).
    """
    program = engine.build()
    with tempfile.TemporaryDirectory(prefix="spz-reference-") as home:
        out = Path(home) / "out.wav"
        command = [program, "--delay=0", "-s", "-rr", f"-f{rate}", f"-t{seconds}",
                   "--residfp" if accurate else "--sidlite"]
        if subtune is not None:
            command.append(f"-o{subtune}")
        command += [*extra, f"-w{out}", tune]
        process = run(command, env={"HOME": home, "XDG_CONFIG_HOME": str(Path(home) / "config")}, timeout=300)
        if process.returncode != 0 or not out.exists():
            raise RuntimeError(f"the reference program could not render {tune}:\n{process.stdout[-600:]}")
        with wave.open(str(out)) as audio:
            if (audio.getnchannels(), audio.getsampwidth(), audio.getframerate()) != (2, 2, rate):
                raise RuntimeError("the reference program did not write 16-bit stereo at the rate asked for")
            return audio.readframes(audio.getnframes())


def first_difference(a, b):
    """The number of the first frame at which two renders differ, or None if they are equal."""
    if a == b:
        return None
    for frame in range(min(len(a), len(b)) // 4):
        if a[frame * 4:frame * 4 + 4] != b[frame * 4:frame * 4 + 4]:
            return frame
    return min(len(a), len(b)) // 4
