# SPDX-License-Identifier: GPL-3.0-or-later
"""The host-package level (TF-070 to TF-073): the .deb for the device, and every check on it."""

from . import deb
from .common import Timer, canaries_wanted, needs
from .unit import python_results


def level():
    needs("cmake", "ninja", "aarch64-linux-gnu-gcc", "aarch64-linux-gnu-g++", "dpkg-deb", "qemu-aarch64-static",
          "python3", "git", "curl", "make", "pkg-config", "file", "strings", "docker")
    with Timer("arm64 containers"):
        deb.container_ready()
    with Timer("the package: cmake --workflow --preset cp0-cross-package"):
        package = deb.build()
    with Timer("the checks on the package"):
        env = {"SPZ_DEB": str(package)}
        canaries = canaries_wanted()
        return python_results("tests/framework/package", canaries, env) + python_results("tests/package", canaries, env)
