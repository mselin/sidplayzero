# SPDX-License-Identifier: GPL-3.0-or-later
"""Reading, writing and comparing the pictures that `shot` writes (SPEC-255, TF-044). Standard library only."""

import struct
import zlib


def read(path):
    """Returns (width, height, rows of RGB bytes). Only the plain PNGs this project writes are understood."""
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path} is not a PNG")
    at, width, height, packed = 8, 0, 0, b""
    while at < len(data):
        size, kind = struct.unpack(">I4s", data[at:at + 8])
        body = data[at + 8:at + 8 + size]
        if kind == b"IHDR":
            width, height, depth, colour, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if (depth, colour, interlace) != (8, 2, 0):
                raise ValueError(f"{path}: only 8-bit RGB without interlace is understood")
        elif kind == b"IDAT":
            packed += body
        at += 12 + size
    raw = zlib.decompress(packed)
    stride = 1 + width * 3
    rows = []
    for y in range(height):
        if raw[y * stride] != 0:
            raise ValueError(f"{path}: a filtered row; only unfiltered rows are understood")
        rows.append(raw[y * stride + 1:(y + 1) * stride])
    return width, height, rows


def write(path, width, height, rows):
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
    raw = b"".join(b"\x00" + bytes(row) for row in rows)
    with open(path, "wb") as out:
        out.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                  + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def compare(got_path, reference_path, difference_path):
    """Returns how many pixels differ. If any do, writes a picture with those pixels in red on a dimmed copy."""
    width, height, got = read(got_path)
    ref_width, ref_height, reference = read(reference_path)
    if (width, height) != (ref_width, ref_height):
        return width * height
    different = 0
    rows = []
    for y in range(height):
        row = bytearray()
        for x in range(width):
            a, b = got[y][x * 3:x * 3 + 3], reference[y][x * 3:x * 3 + 3]
            if a != b:
                different += 1
                row += b"\xff\x00\x00"
            else:
                row += bytes(v // 3 for v in a)
        rows.append(row)
    if different:
        write(difference_path, width, height, rows)
    return different
