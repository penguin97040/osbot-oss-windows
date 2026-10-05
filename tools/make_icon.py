#!/usr/bin/env python3
"""Generates res/icon.ico (a stylised webcam lens) using only the standard library.

Run from the repo root:  python3 tools/make_icon.py
"""
import math
import struct
import zlib

BG = (0x16, 0x1A, 0x20)
RING = (0x4F, 0x8C, 0xFF)
LENS = (0x0F, 0x11, 0x15)
GLINT = (0xE6, 0xE8, 0xEB)
SS = 4  # supersampling factor for anti-aliasing


def sample(x, y, size):
    """Returns RGBA for a point in [0, size) space."""
    c = size / 2.0
    # Rounded-square background.
    r = size * 0.22
    half = size / 2.0
    dx = max(abs(x - c) - (half - r), 0.0)
    dy = max(abs(y - c) - (half - r), 0.0)
    if math.hypot(dx, dy) > r:
        return (0, 0, 0, 0)
    d = math.hypot(x - c, y - c)
    if d < size * 0.13:
        return LENS + (255,)
    gx, gy = c - size * 0.06, c - size * 0.06
    if math.hypot(x - gx, y - gy) < size * 0.045:
        return GLINT + (255,)
    if d < size * 0.20:
        return LENS + (255,)
    if d < size * 0.32:
        return RING + (255,)
    # Status light, top right.
    if math.hypot(x - size * 0.76, y - size * 0.24) < size * 0.055:
        return (0x4C, 0xC9, 0x7C, 255)
    return BG + (255,)


def render(size):
    rows = []
    for py in range(size):
        row = bytearray([0])  # PNG filter type 0
        for px in range(size):
            acc = [0, 0, 0, 0]
            for sy in range(SS):
                for sx in range(SS):
                    r, g, b, a = sample(px + (sx + 0.5) / SS, py + (sy + 0.5) / SS, size)
                    acc[0] += r * a
                    acc[1] += g * a
                    acc[2] += b * a
                    acc[3] += a
            n = SS * SS
            a = acc[3] / n
            if a > 0:
                row += bytes([round(acc[0] / acc[3]), round(acc[1] / acc[3]), round(acc[2] / acc[3]), round(a)])
            else:
                row += bytes(4)
        rows.append(bytes(row))
    return png(size, b"".join(rows))


def png(size, raw):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def main():
    sizes = [16, 24, 32, 48, 64, 256]
    images = [render(s) for s in sizes]
    header = struct.pack("<HHH", 0, 1, len(sizes))
    offset = 6 + 16 * len(sizes)
    entries = b""
    for s, data in zip(sizes, images):
        entries += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    with open("res/icon.ico", "wb") as f:
        f.write(header + entries + b"".join(images))
    print("Wrote res/icon.ico")


if __name__ == "__main__":
    main()
