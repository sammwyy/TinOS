#!/usr/bin/env python3
"""Create a deterministic numbered BMP placeholder for a shell icon.

Usage:
    python3 tools/make_icon_placeholder.py folder
    python3 tools/make_icon_placeholder.py folder 7
"""

from pathlib import Path
import struct
import sys


SIZE = 64
ICON_DIR = Path(__file__).resolve().parent.parent / "resources" / "icons"

# A tiny 5x7 font keeps this tool dependency-free and works in the kernel's
# low-resolution UI.  Each row is a five-bit bitmap, left-to-right.
DIGITS = {
    "0": ("11111", "10001", "10011", "10101", "11001", "10001", "11111"),
    "1": ("00100", "01100", "00100", "00100", "00100", "00100", "01110"),
    "2": ("11110", "00001", "00001", "01110", "10000", "10000", "11111"),
    "3": ("11110", "00001", "00001", "01110", "00001", "00001", "11110"),
    "4": ("10010", "10010", "10010", "11111", "00010", "00010", "00010"),
    "5": ("11111", "10000", "10000", "11110", "00001", "00001", "11110"),
    "6": ("01110", "10000", "10000", "11110", "10001", "10001", "01110"),
    "7": ("11111", "00001", "00010", "00100", "01000", "01000", "01000"),
    "8": ("01110", "10001", "10001", "01110", "10001", "10001", "01110"),
    "9": ("01110", "10001", "10001", "01111", "00001", "00001", "01110"),
}


def color_for(name):
    # FNV-1a: stable across machines and Python versions, unlike hash().
    value = 2166136261
    for byte in name.encode("utf-8"):
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    r = 64 + (value & 0x7F)
    g = 64 + ((value >> 8) & 0x7F)
    b = 64 + ((value >> 16) & 0x7F)
    return r, g, b


def draw_number(pixels, number, foreground):
    text = str(number)
    scale = 4
    glyph_w = 5 * scale
    gap = scale
    total_w = len(text) * glyph_w + (len(text) - 1) * gap
    left = (SIZE - total_w) // 2
    top = (SIZE - 7 * scale) // 2
    for index, digit in enumerate(text):
        glyph = DIGITS[digit]
        x0 = left + index * (glyph_w + gap)
        for gy, row in enumerate(glyph):
            for gx, bit in enumerate(row):
                if bit == "1":
                    for dy in range(scale):
                        for dx in range(scale):
                            pixels[top + gy * scale + dy][x0 + gx * scale + dx] = foreground


def write_bmp(path, pixels):
    row_size = (SIZE * 3 + 3) & ~3
    image_size = row_size * SIZE
    file_size = 54 + image_size
    header = struct.pack(
        "<2sIHHIIiiHHIIiiII",
        b"BM", file_size, 0, 0, 54,
        40, SIZE, SIZE, 1, 24, 0, image_size, 2835, 2835, 0, 0,
    )
    body = bytearray()
    for row in reversed(pixels):
        for r, g, b in row:
            body.extend((b, g, r))
        body.extend(b"\0" * (row_size - SIZE * 3))
    path.write_bytes(header + body)


def main(argv):
    if len(argv) not in (2, 3):
        raise SystemExit(f"usage: {argv[0]} NAME [NUMBER]")
    name = argv[1]
    if not name or name in (".", "..") or "/" in name or "\\" in name:
        raise SystemExit("NAME must be a simple icon name, without a path")

    ICON_DIR.mkdir(parents=True, exist_ok=True)
    if len(argv) == 3:
        try:
            number = int(argv[2])
        except ValueError:
            raise SystemExit("NUMBER must be an integer")
        if number < 0:
            raise SystemExit("NUMBER must be non-negative")
    else:
        number = len(list(ICON_DIR.glob("*.bmp"))) + 1

    background = color_for(name)
    # A dark inset border makes the placeholder readable on any shell panel.
    border = tuple(max(0, channel // 2) for channel in background)
    pixels = []
    for y in range(SIZE):
        row = []
        for x in range(SIZE):
            row.append(border if x in (0, SIZE - 1) or y in (0, SIZE - 1)
                       else background)
        pixels.append(row)

    luminance = (background[0] * 299 + background[1] * 587 + background[2] * 114) // 1000
    foreground = (255, 255, 255) if luminance < 150 else (16, 16, 16)
    draw_number(pixels, number, foreground)

    output = ICON_DIR / f"{name}.bmp"
    write_bmp(output, pixels)
    print(f"{output}: placeholder {number} ({SIZE}x{SIZE} BMP)")


if __name__ == "__main__":
    main(sys.argv)
