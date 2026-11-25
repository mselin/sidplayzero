# SPDX-License-Identifier: GPL-3.0-or-later
"""The engine and the reference program, built from their pinned archives (TF-050; SPEC-002, SPEC-071).

libresidfp, libsidplayfp and the sidplayfp program are built once into .cache/engine-host/ and kept until
a pin changes. The program is what the tests compare the app's sound with; it is never shipped.
"""

import os
import shutil
import tarfile
from pathlib import Path

from . import pins
from .common import CACHE, needs, run, say

PREFIX = CACHE / "engine-host" / "prefix"
SIDPLAYFP = PREFIX / "bin" / "sidplayfp"


def _build(name, source, configure_arguments, env):
    say(f"   building {name} (once)")
    work = CACHE / "engine-host" / "work"
    shutil.rmtree(work / Path(source).name.replace(".tar.gz", ""), ignore_errors=True)
    work.mkdir(parents=True, exist_ok=True)
    with tarfile.open(source) as archive:
        archive.extractall(work, filter="data")
    folder = work / Path(source).name.replace(".tar.gz", "")
    run(["./configure", f"--prefix={PREFIX}", "--quiet", *configure_arguments], cwd=folder, env=env, check=True)
    run(["make", f"-j{os.cpu_count() or 2}"], cwd=folder, env=env, check=True)
    run(["make", "install"], cwd=folder, env=env, check=True)


def build():
    """Returns the path of the reference program, building it and the libraries first if need be."""
    table = pins.load()
    stamp = PREFIX / ".pins"
    wanted = "\n".join(table[name].sha256 for name in ("libresidfp", "libsidplayfp", "sidplayfp"))
    if SIDPLAYFP.exists() and stamp.exists() and stamp.read_text() == wanted:
        return SIDPLAYFP
    needs("make", "pkg-config", "c++", "cc")
    shutil.rmtree(CACHE / "engine-host", ignore_errors=True)
    env = {"PKG_CONFIG_PATH": str(PREFIX / "lib" / "pkgconfig"), "PKG_CONFIG_LIBDIR": str(PREFIX / "lib" / "pkgconfig")}
    _build("libresidfp", pins.fetch("libresidfp"), ["--enable-static", "--disable-shared"], env)
    _build("libsidplayfp", pins.fetch("libsidplayfp"),
           ["--enable-static", "--disable-shared", "--without-exsid", "--without-usbsid"], env)
    _build("the sidplayfp program", pins.fetch("sidplayfp"), [],
           {**env, "LIBS": f"-L{PREFIX / 'lib'} -lresidfp", "PKG_CONFIG_LIBDIR": ""})
    stamp.write_text(wanted)
    return SIDPLAYFP
