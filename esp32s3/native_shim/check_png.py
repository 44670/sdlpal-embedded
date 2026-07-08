#!/usr/bin/env python3
"""Verify a CoreS3 SE native-shim PNG screenshot."""

from __future__ import annotations

import struct
import sys
import zlib
from pathlib import Path


def paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def main() -> int:
    if len(sys.argv) not in (4, 6):
        print("usage: check_png.py IMAGE WIDTH HEIGHT [MIN_NONBLACK MIN_COLORS]", file=sys.stderr)
        return 2

    path = Path(sys.argv[1])
    expected_width = int(sys.argv[2], 0)
    expected_height = int(sys.argv[3], 0)
    min_nonblack = int(sys.argv[4], 0) if len(sys.argv) == 6 else 100
    min_colors = int(sys.argv[5], 0) if len(sys.argv) == 6 else 16
    data = path.read_bytes()
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        print(f"{path}: bad PNG signature", file=sys.stderr)
        return 1

    offset = 8
    width = height = None
    color_type = None
    bit_depth = None
    compressed = bytearray()
    while offset + 12 <= len(data):
        size = struct.unpack_from(">I", data, offset)[0]
        chunk_type = data[offset + 4 : offset + 8]
        payload = data[offset + 8 : offset + 8 + size]
        offset += 12 + size
        if chunk_type == b"IHDR":
            width, height, bit_depth, color_type = struct.unpack_from(">IIBB", payload, 0)
        elif chunk_type == b"IDAT":
            compressed.extend(payload)
        elif chunk_type == b"IEND":
            break

    if (width, height, bit_depth, color_type) != (expected_width, expected_height, 8, 2):
        print(f"{path}: unexpected PNG shape {(width, height, bit_depth, color_type)}", file=sys.stderr)
        return 1

    raw = zlib.decompress(bytes(compressed))
    stride = expected_width * 3
    if len(raw) != (stride + 1) * expected_height:
        print(f"{path}: unexpected decompressed size {len(raw)}", file=sys.stderr)
        return 1

    previous = bytearray(stride)
    distinct = set()
    nonblack = 0
    cursor = 0
    for _ in range(expected_height):
        filter_type = raw[cursor]
        cursor += 1
        row = bytearray(raw[cursor : cursor + stride])
        cursor += stride
        for i, value in enumerate(row):
            left = row[i - 3] if i >= 3 else 0
            up = previous[i]
            up_left = previous[i - 3] if i >= 3 else 0
            if filter_type == 1:
                row[i] = (value + left) & 0xFF
            elif filter_type == 2:
                row[i] = (value + up) & 0xFF
            elif filter_type == 3:
                row[i] = (value + ((left + up) >> 1)) & 0xFF
            elif filter_type == 4:
                row[i] = (value + paeth(left, up, up_left)) & 0xFF
            elif filter_type != 0:
                print(f"{path}: unsupported PNG filter {filter_type}", file=sys.stderr)
                return 1
        for i in range(0, stride, 3):
            pixel = (row[i], row[i + 1], row[i + 2])
            distinct.add(pixel)
            if pixel != (0, 0, 0):
                nonblack += 1
        previous = row

    if nonblack < min_nonblack or len(distinct) < min_colors:
        print(f"{path}: screenshot looks blank: nonblack={nonblack} colors={len(distinct)}", file=sys.stderr)
        return 1
    print(f"{path}: {expected_width}x{expected_height} colors={len(distinct)} nonblack={nonblack}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
