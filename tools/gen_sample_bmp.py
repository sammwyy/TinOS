#!/usr/bin/env python3
"""Build a deterministic 24-bit BMP for the userspace image viewer."""

import struct
import sys
from pathlib import Path

width, height = 128, 96
stride = (width * 3 + 3) & ~3
pixels = bytearray()
for y in range(height):
    for x in range(width):
        red = x * 255 // (width - 1)
        green = y * 255 // (height - 1)
        blue = 128 if ((x // 16) + (y // 16)) & 1 else 224
        pixels.extend((blue, green, red))
    pixels.extend(bytes(stride - width * 3))

file_header = struct.pack("<2sIHHI", b"BM", 54 + len(pixels), 0, 0, 54)
dib_header = struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0,
                         len(pixels), 2835, 2835, 0, 0)
Path(sys.argv[1]).write_bytes(file_header + dib_header + pixels)
