# SPDX-License-Identifier: GPL-3.0-or-later
"""The .deb for the device, and ways of looking into it (TF-070 to TF-072; SPEC-220 to SPEC-227)."""

import json
import lzma
import os
import shutil
import subprocess
import tempfile
import time
from pathlib import Path

from .common import CACHE, ROOT, Missing, run

# The maintainer of the packages the tests build (SPEC-221): ".test" can never be a real address.
TEST_MAINTAINER = "SidPlayZero test build <build@sidplayzero.test>"
# A clean Debian trixie for arm64, from the Docker library's mirror on Amazon's public registry.
IMAGE = "public.ecr.aws/docker/library/debian:trixie-slim"
INDEX_URL = "https://deb.debian.org/debian/dists/trixie/main/binary-arm64/Packages.xz"


def version():
    return json.loads((ROOT / "app-builder.json").read_text())["version"]


def build():
    """Runs the cp0-cross-package build and returns the .deb's path."""
    run(["cmake", "--workflow", "--preset", "cp0-cross-package"], env={"SIDPLAYZERO_MAINTAINER": TEST_MAINTAINER},
        check=True, timeout=3600)
    deb = ROOT / "dist" / f"sidplayzero_{version()}-1_arm64.deb"
    if not deb.exists():
        raise RuntimeError(f"the package build did not write {deb.relative_to(ROOT)}")
    return deb


def files(deb):
    """[(mode, owner, path)] of everything in the package, folders included, without the leading "./"."""
    listing = run(["dpkg-deb", "-c", deb], check=True).stdout
    entries = []
    for line in listing.strip().split("\n"):
        cells = line.split()
        path = cells[5][1:] if cells[5].startswith("./") else "/" + cells[5]
        entries.append((cells[0], cells[1], path.rstrip("/") or "/"))
    return entries


def control(deb):
    """The control fields, as a dict."""
    fields = {}
    key = None
    for line in run(["dpkg-deb", "-f", deb], check=True).stdout.split("\n"):
        if line.startswith(" ") and key:
            fields[key] += "\n" + line.strip()
        elif ":" in line:
            key, value = line.split(":", 1)
            fields[key] = value.strip()
    return fields


def control_files(deb):
    """The names of the files in the package's control part: maintainer scripts would be here."""
    with tempfile.TemporaryDirectory() as folder:
        run(["dpkg-deb", "-e", deb, Path(folder) / "control"], check=True)
        return sorted(p.name for p in (Path(folder) / "control").iterdir())


def unpack(deb, into):
    run(["dpkg-deb", "-x", deb, into], check=True)
    return Path(into)


def repack(deb, into, change):
    """A copy of the package with `change(folder)` applied to its unpacked tree. Returns the copy's path."""
    tree = Path(into) / "tree"
    shutil.rmtree(into, ignore_errors=True)
    Path(into).mkdir(parents=True)
    run(["dpkg-deb", "-R", deb, tree], check=True)
    change(tree)
    copy = Path(into) / Path(deb).name
    run(["dpkg-deb", "--root-owner-group", "-b", tree, copy], check=True)
    return copy


def needed_libraries(program):
    """The shared libraries a program asks for when it starts."""
    readelf = shutil.which("aarch64-linux-gnu-readelf") or "readelf"
    out = run([readelf, "-d", program], check=True).stdout
    return [line.split("[")[1].split("]")[0] for line in out.split("\n") if "(NEEDED)" in line]


def trixie_packages():
    """The names of the packages in Debian trixie's main archive for arm64, and what they provide."""
    path = CACHE / "debian" / "trixie-main-arm64-Packages.xz"
    if not path.exists() or time.time() - path.stat().st_mtime > 7 * 86400:
        path.parent.mkdir(parents=True, exist_ok=True)
        done = subprocess.run(["curl", "-fsSL", "--retry", "3", "-o", str(path) + ".part", INDEX_URL])
        if done.returncode != 0:
            raise RuntimeError("could not fetch Debian's package index: " + INDEX_URL)
        os.replace(str(path) + ".part", path)
    names = set()
    with lzma.open(path, "rt", encoding="utf-8") as index:
        for line in index:
            if line.startswith("Package: "):
                names.add(line[9:].strip())
            elif line.startswith("Provides: "):
                names.update(item.split("(")[0].strip() for item in line[10:].split(","))
    return names


def container_ready():
    """Stops the level with what is missing if this machine cannot run an arm64 Debian container."""
    if shutil.which("docker") is None or run(["docker", "info"]).returncode != 0:
        raise Missing(["docker, with its daemon running"])
    probe = run(["docker", "run", "--rm", "--platform", "linux/arm64", IMAGE, "uname", "-m"], timeout=900)
    if probe.returncode != 0 or "aarch64" not in probe.stdout:
        raise Missing(["arm64 emulation for containers (qemu-user-static, registered with binfmt_misc)"])


def in_container(deb, script, timeout=2400):
    """Runs a shell script in a clean Debian trixie arm64 container, with the package at /dist. Returns the
    finished process. Where this machine reaches the network through a proxy, apt is told to use it."""
    command = ["docker", "run", "--rm", "--platform", "linux/arm64", "-v", f"{Path(deb).parent}:/dist:ro"]
    setup = ""
    proxy = os.environ.get("https_proxy") or os.environ.get("HTTPS_PROXY")
    if proxy:
        bundle = next((p for p in (os.environ.get("SSL_CERT_FILE"), os.environ.get("CURL_CA_BUNDLE"),
                                   "/etc/ssl/certs/ca-certificates.crt") if p and Path(p).is_file()), None)
        command += ["-e", f"https_proxy={proxy}"]
        if "://127.0.0.1" in proxy or "://localhost" in proxy:
            command += ["--network", "host"]      # the proxy listens on this machine's own loopback address
        if bundle:
            command += ["-v", f"{bundle}:/host-ca.crt:ro"]
            setup = ("printf 'Acquire::https::CaInfo \"/host-ca.crt\";\\nAcquire::https::Proxy \"%s\";\\n' \"$https_proxy\""
                     " > /etc/apt/apt.conf.d/99proxy; sed -i 's|http://deb.debian.org|https://deb.debian.org|'"
                     " /etc/apt/sources.list.d/debian.sources; ")
    command += [IMAGE, "sh", "-ec", setup + script]
    return run(command, timeout=timeout)
