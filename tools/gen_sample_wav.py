#!/usr/bin/env python3
"""Build a deterministic 16-bit mono PCM sample for the userspace player."""

import struct
import sys
from pathlib import Path

rate = 22050
notes = (262, 294, 330, 349, 392, 440, 494, 523)
samples = bytearray()
for note in notes:
    for index in range(rate // 4):
        level = 8000 if (index // max(1, rate // note // 2)) & 1 else -8000
        samples.extend(struct.pack("<h", level))

riff = struct.pack("<4sI4s", b"RIFF", 36 + len(samples), b"WAVE")
fmt = struct.pack("<4sIHHIIHH", b"fmt ", 16, 1, 1, rate, rate * 2, 2, 16)
data = struct.pack("<4sI", b"data", len(samples))
Path(sys.argv[1]).write_bytes(riff + fmt + data + samples)
