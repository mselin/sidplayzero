#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Makes the app's fonts (SPEC-010): LVGL 9.5 bitmap fonts, as C sources, from DejaVu Sans at 12 and 14 pixels.

    python3 tools/make_fonts.py ARCHIVE OUT_FOLDER

ARCHIVE is DejaVu's release archive, pinned in cmake/pins.cmake; `cmake -P cmake/fonts.cmake` downloads and
checks it and then runs this script with src/ui/fonts as OUT_FOLDER. The sources it writes are kept in the
repository, and tests/simulator/test_fonts_source.py checks that making them again gives the same bytes.

Only the Python standard library is used. The outlines are read from the TrueType file and filled here,
without the font's own hinting: each pixel's coverage is the area of the glyph over it (exact across, 16
steps down), with the non-zero rule, in 16 levels (4 bits a pixel). Up and down only, the outlines are
fitted to the pixels first, as a light hinting does: the baseline, the x-height, the capital height and
the ascender land on whole pixels, and the overshoot of round letters past them is taken out. Only exact binary arithmetic and comparisons are
done on the outlines, so the same archive gives the same bytes on every machine. Advances are whole
pixels; the kerning pairs of the font's `kern` table between the glyphs made are kept.
"""

import math
import struct
import sys
import tarfile
from pathlib import Path

FONT_MEMBER = "ttf/DejaVuSans.ttf"
SIZES = (12, 14)
BPP = 4
SUBROWS = 16             # samples down each pixel row
FLATNESS = 1 / 32        # how far a straight piece may stray from a curve, in pixels


def characters():
    """What the fonts hold, in order: the 218 printable characters of Windows-1252 as Unicode, and the
    symbols the screens use that are not among them."""
    wanted = set(range(0x20, 0x7F)) | set(range(0xA0, 0x100))
    for byte in range(0x80, 0xA0):
        try:
            wanted.add(ord(bytes([byte]).decode("cp1252")))
        except UnicodeDecodeError:
            pass                                    # the five bytes Windows-1252 leaves undefined
    assert len(wanted) == 218
    wanted.add(0x0336)      # COMBINING LONG STROKE OVERLAY: a muted voice's number, struck through (SPEC-091)
    wanted.add(0xFFFD)      # REPLACEMENT CHARACTER: what the playlist importers put for bytes that are not text
    wanted.add(0x2193)      # DOWNWARDS ARROW: the job mark on the top line (SPEC-147)
    return sorted(wanted)


# ---------------------------------------------------------------------------------------------------------
# Reading the TrueType file


class TrueType:
    def __init__(self, data):
        self.data = data
        count = struct.unpack_from(">H", data, 4)[0]
        self.tables = {}
        for i in range(count):
            tag, _, offset, length = struct.unpack_from(">4sIII", data, 12 + 16 * i)
            self.tables[tag.decode("latin-1")] = (offset, length)
        head = self.tables["head"][0]
        self.units_per_em = struct.unpack_from(">H", data, head + 18)[0]
        self.long_loca = struct.unpack_from(">h", data, head + 50)[0] == 1
        hhea = self.tables["hhea"][0]
        self.ascender, self.descender = struct.unpack_from(">hh", data, hhea + 4)
        self.long_metrics = struct.unpack_from(">H", data, hhea + 34)[0]
        self.glyph_count = struct.unpack_from(">H", data, self.tables["maxp"][0] + 4)[0]
        self.cmap = self._read_cmap()
        self.kerning = self._read_kern()

    def _read_cmap(self):
        base = self.tables["cmap"][0]
        count = struct.unpack_from(">H", self.data, base + 2)[0]
        for i in range(count):
            platform, encoding, offset = struct.unpack_from(">HHI", self.data, base + 4 + 8 * i)
            if (platform, encoding) == (3, 1) and struct.unpack_from(">H", self.data, base + offset)[0] == 4:
                return self._read_cmap_4(base + offset)
        raise ValueError("the font has no Unicode cmap of format 4")

    def _read_cmap_4(self, at):
        data = self.data
        segments = struct.unpack_from(">H", data, at + 6)[0] // 2
        ends = at + 14
        starts = ends + 2 * segments + 2
        deltas = starts + 2 * segments
        range_offsets = deltas + 2 * segments
        mapping = {}
        for s in range(segments):
            end = struct.unpack_from(">H", data, ends + 2 * s)[0]
            start = struct.unpack_from(">H", data, starts + 2 * s)[0]
            delta = struct.unpack_from(">h", data, deltas + 2 * s)[0]
            range_offset = struct.unpack_from(">H", data, range_offsets + 2 * s)[0]
            for code in range(start, end + 1):
                if code == 0xFFFF:
                    continue
                if range_offset == 0:
                    glyph = (code + delta) & 0xFFFF
                else:
                    where = range_offsets + 2 * s + range_offset + 2 * (code - start)
                    glyph = struct.unpack_from(">H", data, where)[0]
                    if glyph:
                        glyph = (glyph + delta) & 0xFFFF
                if glyph:
                    mapping[code] = glyph
        return mapping

    def _read_kern(self):
        """The pairs of the first horizontal format-0 subtable of the `kern` table, if there is one."""
        if "kern" not in self.tables:
            return {}
        data = self.data
        at = self.tables["kern"][0]
        count = struct.unpack_from(">H", data, at + 2)[0]
        at += 4
        for _ in range(count):
            length, coverage = struct.unpack_from(">HH", data, at + 2)
            if coverage >> 8 == 0 and coverage & 0x7 == 0x1:            # format 0, horizontal, kerning values
                pairs = struct.unpack_from(">H", data, at + 6)[0]
                kerning = {}
                for p in range(pairs):
                    left, right, value = struct.unpack_from(">HHh", data, at + 14 + 6 * p)
                    kerning[(left, right)] = value
                return kerning
            at += length
        return {}

    def advance(self, glyph):
        hmtx = self.tables["hmtx"][0]
        return struct.unpack_from(">H", self.data, hmtx + 4 * min(glyph, self.long_metrics - 1))[0]

    def _glyph_span(self, glyph):
        loca = self.tables["loca"][0]
        if self.long_loca:
            start, end = struct.unpack_from(">II", self.data, loca + 4 * glyph)
        else:
            start, end = (2 * v for v in struct.unpack_from(">HH", self.data, loca + 2 * glyph))
        return self.tables["glyf"][0] + start, end - start

    def contours(self, glyph, depth=0):
        """The glyph's outline as closed contours of (x, y, on_curve) points, in font units."""
        if depth > 8:
            raise ValueError("components nested too deep")
        at, length = self._glyph_span(glyph)
        if length == 0:
            return []
        data = self.data
        count = struct.unpack_from(">h", data, at)[0]
        if count >= 0:
            return self._simple(at, count)
        return self._composite(at, depth)

    def _simple(self, at, count):
        data = self.data
        ends = struct.unpack_from(">%dH" % count, data, at + 10)
        points = ends[-1] + 1 if count else 0
        p = at + 10 + 2 * count
        p += 2 + struct.unpack_from(">H", data, p)[0]                  # the instructions, not used
        flags = []
        while len(flags) < points:
            flag = data[p]
            p += 1
            repeat = 0
            if flag & 0x08:
                repeat = data[p]
                p += 1
            flags.extend([flag] * (repeat + 1))
        flags = flags[:points]

        def coordinates(short, same):
            nonlocal p
            values, value = [], 0
            for flag in flags:
                if flag & short:
                    step = data[p]
                    p += 1
                    value += step if flag & same else -step
                elif not flag & same:
                    value += struct.unpack_from(">h", data, p)[0]
                    p += 2
                values.append(value)
            return values

        xs = coordinates(0x02, 0x10)
        ys = coordinates(0x04, 0x20)
        contours, first = [], 0
        for end in ends:
            contours.append([(xs[i], ys[i], bool(flags[i] & 1)) for i in range(first, end + 1)])
            first = end + 1
        return contours

    def _composite(self, at, depth):
        data = self.data
        p = at + 10
        contours = []
        while True:
            flags, component = struct.unpack_from(">HH", data, p)
            p += 4
            if flags & 0x0001:
                dx, dy = struct.unpack_from(">hh", data, p)
                p += 4
            else:
                dx, dy = struct.unpack_from(">bb", data, p)
                p += 2
            if not flags & 0x0002:
                raise ValueError("a component placed by matching points is not supported")
            a, b, c, d = 1.0, 0.0, 0.0, 1.0
            if flags & 0x0008:
                a = d = struct.unpack_from(">h", data, p)[0] / 16384
                p += 2
            elif flags & 0x0040:
                a, d = (v / 16384 for v in struct.unpack_from(">hh", data, p))
                p += 4
            elif flags & 0x0080:
                a, b, c, d = (v / 16384 for v in struct.unpack_from(">hhhh", data, p))
                p += 8
            for contour in self.contours(component, depth + 1):
                contours.append([(a * x + c * y + dx, b * x + d * y + dy, on) for x, y, on in contour])
            if not flags & 0x0020:
                break
        return contours


# ---------------------------------------------------------------------------------------------------------
# Filling an outline


class Vertical:
    """Where a height in font units lands, in pixels: the font's heights (the baseline, x-height, capital
    height and ascender, measured on H, x and l) on whole pixels, the overshoot of round letters past each
    (measured on o) flattened onto it, and straight lines between them; beyond, the plain scale."""

    def __init__(self, font, scale):
        def bounds(letter):
            ys = [y for contour in font.contours(font.cmap[ord(letter)]) for _, y, _ in contour]
            return min(ys), max(ys)

        base, x_height = bounds("x")
        capital = bounds("H")[1]
        ascender = bounds("l")[1]
        low, high = bounds("o")
        under, over = base - low, high - x_height
        self.scale = scale
        knots = [(base - under, base), (base, base)]
        for height in (x_height, capital, ascender):
            knots += [(height, height), (height + over, height)]
        self.knots = [(u, math.floor(v * scale + 0.5)) for u, v in knots]

    def __call__(self, y):
        knots = self.knots
        if y <= knots[0][0]:
            return knots[0][1] + (y - knots[0][0]) * self.scale
        for (u0, v0), (u1, v1) in zip(knots, knots[1:]):
            if y <= u1:
                return v0 if u1 == u0 else v0 + (y - u0) * (v1 - v0) / (u1 - u0)
        return knots[-1][1] + (y - knots[-1][0]) * self.scale


def segments(contours, scale, vertical):
    """The outline as straight edges, in pixels with y going up: quadratic curves are cut into pieces."""
    edges = []
    for contour in contours:
        if len(contour) < 2:
            continue
        points = [(x * scale, vertical(y), on) for x, y, on in contour]
        # Start at a point on the curve; if there is none, at the middle of the first two.
        start = next((i for i, point in enumerate(points) if point[2]), None)
        if start is None:
            x0, y0, _ = points[0]
            x1, y1, _ = points[1]
            points = [((x0 + x1) / 2, (y0 + y1) / 2, True)] + points[1:] + points[:1]
        else:
            points = points[start:] + points[:start]
        points.append(points[0])
        current = points[0][:2]
        control = None
        for x, y, on in points[1:]:
            if on:
                if control is None:
                    edges.append((current, (x, y)))
                else:
                    edges.extend(curve(current, control, (x, y)))
                    control = None
                current = (x, y)
            else:
                if control is not None:
                    middle = ((control[0] + x) / 2, (control[1] + y) / 2)
                    edges.extend(curve(current, control, middle))
                    current = middle
                control = (x, y)
        if control is not None:
            edges.extend(curve(current, control, points[0][:2]))
    return edges


def curve(p0, p1, p2):
    """A quadratic curve as straight pieces, as many as keep within FLATNESS of it."""
    ex = p0[0] - 2 * p1[0] + p2[0]
    ey = p0[1] - 2 * p1[1] + p2[1]
    deviation = math.sqrt(ex * ex + ey * ey) / 4
    count = max(1, math.ceil(math.sqrt(deviation / FLATNESS)))
    pieces, last = [], p0
    for i in range(1, count + 1):
        t = i / count
        u = 1 - t
        point = (u * u * p0[0] + 2 * u * t * p1[0] + t * t * p2[0],
                 u * u * p0[1] + 2 * u * t * p1[1] + t * t * p2[1])
        pieces.append((last, point))
        last = point
    return pieces


def fill(edges, left, bottom, width, height):
    """The coverage of each pixel of a box, from 0 to 1, top row first. The box's lower left corner is at
    (left, bottom), in pixels with y going up."""
    rows = [[0.0] * width for _ in range(height)]
    edges = [(a, b) for a, b in edges if a[1] != b[1]]
    for row in range(height):
        top = bottom + height - row
        cells = rows[row]
        for sub in range(SUBROWS):
            y = top - (sub + 0.5) / SUBROWS
            crossings = []
            for (x0, y0), (x1, y1) in edges:
                if (y0 <= y < y1) or (y1 <= y < y0):
                    x = x0 + (y - y0) * (x1 - x0) / (y1 - y0)
                    crossings.append((x, 1 if y1 > y0 else -1))
            crossings.sort()
            winding = 0
            for i, (x, direction) in enumerate(crossings):
                was = winding
                winding += direction
                if was == 0 and winding != 0:
                    span_start = x
                elif was != 0 and winding == 0:
                    add_span(cells, span_start - left, x - left)
    return rows


def add_span(cells, a, b):
    """Adds a span from a to b, measured from the box's left edge, to one sub-row's share of the cells."""
    a = max(a, 0.0)
    b = min(b, float(len(cells)))
    if b <= a:
        return
    share = 1.0 / SUBROWS
    first, last = int(a), int(b)
    if first == last:
        cells[first] += (b - a) * share
        return
    cells[first] += (first + 1 - a) * share
    for i in range(first + 1, last):
        cells[i] += share
    if last < len(cells):
        cells[last] += (b - last) * share


# ---------------------------------------------------------------------------------------------------------
# One glyph, one font


def render(font, glyph, scale, vertical):
    """(advance in pixels, box_w, box_h, ofs_x, ofs_y, levels top row first)."""
    advance = math.floor(font.advance(glyph) * scale + 0.5)
    edges = segments(font.contours(glyph), scale, vertical)
    if not edges:
        return advance, 0, 0, 0, 0, []
    xs = [x for edge in edges for x, _ in edge]
    ys = [y for edge in edges for _, y in edge]
    left, right = math.floor(min(xs)), math.ceil(max(xs))
    bottom, top = math.floor(min(ys)), math.ceil(max(ys))
    width, height = right - left, top - bottom
    coverage = fill(edges, left, bottom, width, height)
    levels = [[min(15, math.floor(min(c, 1.0) * 15 + 0.5)) for c in row] for row in coverage]
    # Leave out empty rows and columns at the edges.
    while levels and not any(levels[0]):
        levels.pop(0)
    while levels and not any(levels[-1]):
        levels.pop()
        bottom += 1
    if not levels:
        return advance, 0, 0, 0, 0, []
    while not any(row[0] for row in levels):
        levels = [row[1:] for row in levels]
        left += 1
    while not any(row[-1] for row in levels):
        levels = [row[:-1] for row in levels]
    return advance, len(levels[0]), len(levels), left, bottom, levels


def pack(levels):
    """4-bit levels, rows one after the other with no padding, the first pixel in the high half."""
    flat = [v for row in levels for v in row]
    if len(flat) % 2:
        flat.append(0)
    return bytes((flat[i] << 4) | flat[i + 1] for i in range(0, len(flat), 2))


def name_of(code):
    if code == 0x22:
        return '"\\""'
    if code == 0x5C:
        return '"\\\\"'
    if code == 0x2F:
        return '"/"'
    if 0x20 < code < 0x7F:
        return '"%s"' % chr(code)
    return ""


def cmaps_of(codes):
    """The character maps, for codes in order with glyph ids 1, 2, ...: a run of three or more codes in a row
    is a range of its own, and the codes between two such runs are one sparse map."""
    runs, start = [], 0
    for i in range(1, len(codes) + 1):
        if i == len(codes) or codes[i] != codes[i - 1] + 1:
            runs.append((start, i))
            start = i
    maps, loose = [], []
    for first, end in runs:
        if end - first >= 3:
            if loose:
                maps.append(("sparse", loose[0], loose[-1] + 1))
                loose = []
            maps.append(("range", first, end))
        else:
            loose.extend(range(first, end))
    if loose:
        maps.append(("sparse", loose[0], loose[-1] + 1))
    return maps


def make_font(font, size, archive_name):
    codes = characters()
    scale = size / font.units_per_em
    ascent = math.ceil(font.ascender * scale)
    descent = math.ceil(-font.descender * scale)
    vertical = Vertical(font, scale)
    glyphs = []
    for code in codes:
        if code not in font.cmap:
            raise ValueError("DejaVu Sans has no glyph for U+%04X" % code)
        glyphs.append(render(font, font.cmap[code], scale, vertical))
    ident = "spz_font_dejavu_sans_%d" % size

    out = []
    w = out.append
    w("/* SPDX-License-Identifier: Bitstream-Vera")
    w(" *")
    w(" * DejaVu Sans at %d pixels, a bitmap font for LVGL 9.5, made by tools/make_fonts.py from %s" % (size, archive_name))
    w(" * (SPEC-010). Do not edit: make it again with `cmake -P cmake/fonts.cmake`. The glyphs are DejaVu's, under")
    w(" * the Bitstream Vera licence; see packaging/THIRD-PARTY-NOTICES.md.")
    w(" * %d glyphs, %d bits a pixel, line height %d, baseline %d from the bottom." % (len(codes), BPP, ascent + descent, descent))
    w(" */")
    w("")
    w('#include "lvgl.h"')
    w("")
    w("/* The glyphs' pictures. */")
    w("static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {")
    offsets = []
    total = 0
    for code, (_, bw, bh, _, _, levels) in zip(codes, glyphs):
        data = pack(levels) if bw and bh else b""
        offsets.append(total)
        total += len(data)
        w("    /* U+%04X %s */" % (code, name_of(code)))
        for i in range(0, len(data), 16):
            w("    " + ", ".join("0x%02x" % b for b in data[i:i + 16]) + ",")
    if total == 0:
        w("    0")
    w("};")
    w("")
    w("/* Each glyph's place in glyph_bitmap, advance (in sixteenths of a pixel), box and offset. */")
    w("static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {")
    w("    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,")
    for code, offset, (adv, bw, bh, ox, oy, _) in zip(codes, offsets, glyphs):
        w("    {.bitmap_index = %d, .adv_w = %d, .box_w = %d, .box_h = %d, .ofs_x = %d, .ofs_y = %d} /* U+%04X */,"
          % (offset, adv * 16, bw, bh, ox, oy, code))
    w("};")
    w("")
    maps = cmaps_of(codes)
    for number, (kind, first, end) in enumerate(maps):
        if kind == "sparse":
            base = codes[first]
            w("static const uint16_t unicode_list_%d[] = {" % number)
            listed = ["0x%x" % (codes[i] - base) for i in range(first, end)]
            for i in range(0, len(listed), 8):
                w("    " + ", ".join(listed[i:i + 8]) + ",")
            w("};")
            w("")
    w("/* Which glyph each character has. */")
    w("static const lv_font_fmt_txt_cmap_t cmaps[] = {")
    for number, (kind, first, end) in enumerate(maps):
        base = codes[first]
        length = codes[end - 1] - base + 1
        if kind == "range":
            w("    {.range_start = %d, .range_length = %d, .glyph_id_start = %d, .unicode_list = NULL,"
              % (base, length, first + 1))
            w("     .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY},")
        else:
            w("    {.range_start = %d, .range_length = %d, .glyph_id_start = %d, .unicode_list = unicode_list_%d,"
              % (base, length, first + 1, number))
            w("     .glyph_id_ofs_list = NULL, .list_length = %d, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY}," % (end - first))
    w("};")
    w("")
    ids = {font.cmap[code]: number + 1 for number, code in enumerate(codes)}
    pairs = []
    for (left, right), value in sorted(font.kerning.items()):
        if left in ids and right in ids:
            sixteenths = max(-128, min(127, math.floor(value * scale * 16 + 0.5)))
            if sixteenths:
                pairs.append((ids[left], ids[right], sixteenths))
    pairs.sort()
    wide = len(codes) + 1 > 255
    if pairs:
        w("/* Kerning, from the font's kern table: pairs of glyph ids in order, and sixteenths of a pixel. */")
        w("static const %s kern_pair_glyph_ids[] = {" % ("uint16_t" if wide else "uint8_t"))
        listed = ["%d, %d" % (a, b) for a, b, _ in pairs]
        for i in range(0, len(listed), 8):
            w("    " + ", ".join(listed[i:i + 8]) + ",")
        w("};")
        w("")
        w("static const int8_t kern_pair_values[] = {")
        listed = ["%d" % v for _, _, v in pairs]
        for i in range(0, len(listed), 16):
            w("    " + ", ".join(listed[i:i + 16]) + ",")
        w("};")
        w("")
        w("static const lv_font_fmt_txt_kern_pair_t kern_pairs = {")
        w("    .glyph_ids = kern_pair_glyph_ids, .values = kern_pair_values, .pair_cnt = %d, .glyph_ids_size = %d,"
          % (len(pairs), 1 if wide else 0))
        w("};")
        w("")
    w("static const lv_font_fmt_txt_dsc_t font_dsc = {")
    w("    .glyph_bitmap = glyph_bitmap,")
    w("    .glyph_dsc = glyph_dsc,")
    w("    .cmaps = cmaps,")
    w("    .kern_dsc = %s," % ("&kern_pairs" if pairs else "NULL"))
    w("    .kern_scale = 16,")
    w("    .cmap_num = %d," % len(maps))
    w("    .bpp = %d," % BPP)
    w("    .kern_classes = 0,")
    w("    .bitmap_format = 0,")
    w("    .stride = 0,")
    w("};")
    w("")
    w("const lv_font_t %s = {" % ident)
    w("    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,")
    w("    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,")
    w("    .line_height = %d," % (ascent + descent))
    w("    .base_line = %d," % descent)
    w("    .subpx = LV_FONT_SUBPX_NONE,")
    w("    .underline_position = -1,")
    w("    .underline_thickness = 1,")
    w("    .dsc = &font_dsc,")
    w("};")
    return "\n".join(out) + "\n"


def main(argv):
    if len(argv) != 3:
        print(__doc__.strip().split("\n\n")[1], file=sys.stderr)
        return 2
    archive, folder = Path(argv[1]), Path(argv[2])
    with tarfile.open(archive) as tar:
        member = next((m for m in tar.getmembers() if m.isfile() and m.name.endswith("/" + FONT_MEMBER)), None)
        if member is None:
            raise SystemExit(f"{archive.name} holds no {FONT_MEMBER}")
        font = TrueType(tar.extractfile(member).read())
    folder.mkdir(parents=True, exist_ok=True)
    for size in SIZES:
        text = make_font(font, size, archive.name)
        (folder / f"dejavu_sans_{size}.c").write_bytes(text.encode("ascii"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
