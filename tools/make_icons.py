#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Draws SidPlayZero's icons: the three wave shapes of a SID voice (sawtooth, pulse, triangle), one above
the other, on a dark tile. The project's own drawing, made here so that it can be made again (TF-070).

    python3 tools/make_icons.py     writes packaging/icons/: sidplayzero.png and sidplayzero_100.png (100
                                    pixels), sidplayzero_80.png (80), for the launcher (SPEC-222), and
                                    store.png (256) for the store's listing (SPEC-223)
"""

import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OVERSAMPLE = 4
BACKGROUND = (16, 16, 24)
TILE = (30, 34, 54)
WAVES = [  # (colour, shape)
    ((92, 214, 255), "sawtooth"),
    ((255, 214, 92), "pulse"),
    ((255, 120, 190), "triangle"),
]


def wave_y(shape, phase):
    """The height of a wave, 0 to 1, at a phase of 0 to 1 within its period."""
    if shape == "sawtooth":
        return phase
    if shape == "pulse":
        return 1.0 if phase < 0.5 else 0.0
    return 2 * phase if phase < 0.5 else 2 - 2 * phase


def draw(size):
    big = size * OVERSAMPLE
    pixels = [[BACKGROUND] * big for _ in range(big)]
    radius = big * 0.16
    for y in range(big):
        for x in range(big):
            # a tile with rounded corners
            dx = max(radius - x, 0, x - (big - 1 - radius))
            dy = max(radius - y, 0, y - (big - 1 - radius))
            if dx * dx + dy * dy <= radius * radius:
                pixels[y][x] = TILE
    margin = big * 0.14
    band = (big - 2 * margin) / len(WAVES)
    thickness = max(2, int(big * 0.035))
    periods = 2.5
    for index, (colour, shape) in enumerate(WAVES):
        top = margin + band * index + band * 0.18
        height = band * 0.64
        previous = None
        for x in range(int(margin), int(big - margin)):
            phase = ((x - margin) / (big - 2 * margin) * periods) % 1.0
            y = int(top + height * (1 - wave_y(shape, phase)))
            low, high = (y, y) if previous is None else (min(previous, y), max(previous, y))
            for yy in range(low - thickness // 2, high + thickness // 2 + 1):
                for xx in range(x - thickness // 2, x + thickness // 2 + 1):
                    if 0 <= yy < big and 0 <= xx < big:
                        pixels[yy][xx] = colour
            previous = y
    rows = []
    for y in range(size):
        row = bytearray()
        for x in range(size):
            block = [pixels[y * OVERSAMPLE + j][x * OVERSAMPLE + i] for j in range(OVERSAMPLE) for i in range(OVERSAMPLE)]
            row += bytes(sum(p[c] for p in block) // len(block) for c in range(3))
        rows.append(bytes(row))
    return rows


def write_png(path, size, rows):
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
    raw = b"".join(b"\x00" + row for row in rows)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main():
    folder = ROOT / "packaging" / "icons"
    folder.mkdir(parents=True, exist_ok=True)
    for name, size in (("sidplayzero.png", 100), ("sidplayzero_100.png", 100), ("sidplayzero_80.png", 80), ("store.png", 256)):
        write_png(folder / name, size, draw(size))
        print(f"wrote packaging/icons/{name} ({size}×{size})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
