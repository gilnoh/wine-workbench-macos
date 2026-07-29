#!/usr/bin/env python3
"""Render the raw BGRA surface dumps written by macdrv_surface_flush.

The window surface is what winemac.drv hands to CoreGraphics, so it answers
the question "was this ever drawn?" directly, rather than inferring from an
application's error messages.

Filenames carry the geometry:  surf-<hwnd>-<seq>-<w>x<h>-<stride>.raw

Usage:  tools/probes/rawtopng.py <dump-dir> [out-dir]

Also prints, per dump, the fraction of pixels that differ from the most common
colour -- a quick way to tell a blank surface from one with real content.
"""
import os
import re
import struct
import sys
import zlib
from collections import Counter

NAME_RE = re.compile(r"surf-(?P<hwnd>[0-9A-Fa-fx]+)-(?P<seq>\d+)-(?P<w>\d+)x(?P<h>\d+)-(?P<stride>\d+)\.raw$")


def write_png(path, width, height, rgb_rows):
    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + row for row in rgb_rows)
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 6))
           + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


def convert(src, dst_dir):
    m = NAME_RE.search(os.path.basename(src))
    if not m:
        return None
    w, h, stride = int(m["w"]), int(m["h"]), int(m["stride"])
    data = open(src, "rb").read()
    if len(data) < stride * h:
        return None

    rows, counter = [], Counter()
    for y in range(h):
        base = y * stride
        row = bytearray(w * 3)
        for x in range(w):
            b, g, r = data[base + x * 4], data[base + x * 4 + 1], data[base + x * 4 + 2]
            row[x * 3:x * 3 + 3] = bytes((r, g, b))
            counter[(r, g, b)] += 1
        rows.append(bytes(row))

    total = w * h
    dominant = counter.most_common(1)[0][1] if counter else total
    varied = 100.0 * (total - dominant) / total if total else 0.0

    out = os.path.join(dst_dir, os.path.basename(src)[:-4] + ".png")
    write_png(out, w, h, rows)
    return out, w, h, varied, counter.most_common(1)[0][0]


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    src_dir = sys.argv[1]
    dst_dir = sys.argv[2] if len(sys.argv) > 2 else src_dir
    os.makedirs(dst_dir, exist_ok=True)

    for name in sorted(os.listdir(src_dir)):
        if not name.endswith(".raw"):
            continue
        res = convert(os.path.join(src_dir, name), dst_dir)
        if res:
            out, w, h, varied, dom = res
            print(f"{os.path.basename(out):55s} {w}x{h}  non-dominant {varied:5.1f}%  bg=rgb{dom}")


if __name__ == "__main__":
    main()
