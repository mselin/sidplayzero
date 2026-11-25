#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds sidplayzero-bench (bench.cpp), the engine benchmark of SPEC-262.
#
#   tools/engine-bench/build.sh            a static arm64 program, to copy to a CardputerZero
#   tools/engine-bench/build.sh --host     a program for this machine
#
# It downloads the two engine archives that the specification pins (SPEC-002), checks them against the
# SHA-256 sums given there, builds them as static libraries without the exSID and USBSID backends, and
# links the benchmark against them. The result is tools/engine-bench/dist/sidplayzero-bench (or
# sidplayzero-bench-host). Nothing is installed and nothing outside tools/engine-bench is written.
#
# Needs: curl, make, pkg-config, a C++ compiler; for arm64, the aarch64-linux-gnu cross compiler
# (Debian and Ubuntu: apt install g++-aarch64-linux-gnu).
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
target=arm64
[ "${1:-}" = "--host" ] && target=host

sidplayfp_url="https://github.com/libsidplayfp/libsidplayfp/releases/download/v3.1.2/libsidplayfp-3.1.2.tar.gz"
sidplayfp_sum="6d16c20e523ac4d3e385531dafe247a830b59bcb4eb73a557f15cf4e7ae6da84"
residfp_url="https://github.com/libsidplayfp/libresidfp/releases/download/v1.3.0/libresidfp-1.3.0.tar.gz"
residfp_sum="6de8880dcfc11b993107f71b538262f08ae07fd0e595297ce776f641b5435b38"

work="$here/build/$target"
prefix="$work/prefix"
dist="$here/dist"
mkdir -p "$here/build/archives" "$work" "$dist"

fetch() {  # url sum
    local file
    file="$here/build/archives/$(basename "$1")"
    [ -f "$file" ] || curl -fsSL -o "$file" "$1"
    echo "$2  $file" | sha256sum -c --quiet - || { echo "checksum mismatch: $file" >&2; rm -f "$file"; exit 1; }
    echo "$file"
}

if [ "$target" = arm64 ]; then
    command -v aarch64-linux-gnu-g++ >/dev/null || { echo "aarch64-linux-gnu-g++ is not installed" >&2; exit 1; }
    host_flag=(--host=aarch64-linux-gnu)
    cxx=aarch64-linux-gnu-g++
    static=(-static)
    out="$dist/sidplayzero-bench"
else
    host_flag=()
    cxx="${CXX:-g++}"
    static=()
    out="$dist/sidplayzero-bench-host"
fi

build_library() {  # archive, then extra configure options
    local archive="$1"
    shift
    local name
    name="$(basename "$archive" .tar.gz)"
    rm -rf "${work:?}/$name"
    tar -xzf "$archive" -C "$work"
    (
        cd "$work/$name"
        PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig" PKG_CONFIG_PATH="" ./configure "${host_flag[@]}" --prefix="$prefix" \
            --enable-static --disable-shared --quiet "$@" >/dev/null
        make -j"$(nproc)" >"$work/$name.log" 2>&1 || { tail -30 "$work/$name.log" >&2; exit 1; }
        make install >/dev/null 2>&1
    )
}

residfp_archive="$(fetch "$residfp_url" "$residfp_sum")"
sidplayfp_archive="$(fetch "$sidplayfp_url" "$sidplayfp_sum")"

if [ ! -f "$prefix/lib/libsidplayfp.a" ]; then
    echo "building libresidfp 1.3.0 for $target"
    build_library "$residfp_archive"
    echo "building libsidplayfp 3.1.2 for $target"
    build_library "$sidplayfp_archive" --without-exsid --without-usbsid
fi

echo "building the benchmark"
"$cxx" -std=c++17 -O2 -Wall -Wextra "${static[@]}" -I"$prefix/include" "$here/bench.cpp" \
    -L"$prefix/lib" -lsidplayfp -lresidfp -lpthread -o "$out"

echo "built $out"
if [ "$target" = arm64 ]; then
    echo "copy it to the device and run:  ./tools/device-probe.sh --engine-bench ./sidplayzero-bench"
fi
