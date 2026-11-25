# SPDX-License-Identifier: GPL-3.0-or-later
"""The pinned downloads, read from cmake/pins.cmake so that the build and the tests cannot disagree (TF-004)."""

import hashlib
import re
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path

from .common import CACHE, ROOT

PIN = re.compile(r'spz_pin\(\s*(\w+)\s+"([^"]+)"\s+"([0-9a-f]{64})"\s*\)')
SETTING = re.compile(r'set\(\s*(SPZ_\w+)\s+"([^"]+)"\s*\)')


@dataclass
class Pin:
    name: str
    url: str
    sha256: str


def load():
    text = (ROOT / "cmake" / "pins.cmake").read_text(encoding="utf-8")
    return {name: Pin(name, url, sha) for name, url, sha in PIN.findall(text)}


def settings():
    text = (ROOT / "cmake" / "pins.cmake").read_text(encoding="utf-8")
    return dict(SETTING.findall(text))


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as source:
        for block in iter(lambda: source.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def fetch(name):
    """The path of a pinned file in the cache, downloading it first if need be. A file whose checksum
    differs from its pin is removed and refused."""
    pin = load()[name]
    path = CACHE / "downloads" / name / pin.url.rsplit("/", 1)[1]
    if path.exists() and sha256_of(path) != pin.sha256:
        path.unlink()
    if not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        partial = path.with_suffix(path.suffix + ".part")
        curl = shutil.which("curl")
        if curl is None:
            raise RuntimeError("curl is needed to download " + pin.url)
        done = subprocess.run([curl, "-fsSL", "--retry", "3", "-o", str(partial), pin.url])
        if done.returncode != 0:
            partial.unlink(missing_ok=True)
            raise RuntimeError(f"could not download {name} from {pin.url}")
        have = sha256_of(partial)
        if have != pin.sha256:
            partial.unlink()
            raise RuntimeError(f"checksum mismatch for {name}: expected {pin.sha256}, got {have}")
        partial.rename(path)
    return path
