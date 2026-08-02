#!/usr/bin/env python3
"""Find text in a TinOS screenshot.

The kernel renders text with a fixed 8x16 one-bit font, so a screenshot can
be searched for an exact string by template matching with the very same
glyphs the kernel uses (kernel/gfx/font8x16.c).  That lets the automated
tests assert on what is actually on screen instead of eyeballing pictures.

    tools/screen_ocr.py shot.png "kernel test"
"""

import os
import re
import sys

import numpy as np
from PIL import Image

FONT_C = os.path.join(os.path.dirname(__file__), "..", "kernel", "gfx",
                      "font8x16.c")

_glyph_cache = None


def load_font(path=FONT_C):
    """Parse the generated font table into a {code: (16,8) bool array}."""
    global _glyph_cache
    if _glyph_cache is not None:
        return _glyph_cache

    glyphs = {}
    with open(path) as f:
        for line in f:
            m = re.match(r"\s*/\* 0x([0-9A-F]{2}).*?\*/\s*\{(.*)\},", line)
            if not m:
                continue
            code = int(m.group(1), 16)
            values = [int(v, 16) for v in m.group(2).split(",")]
            rows = np.zeros((16, 8), dtype=bool)
            for y, bits in enumerate(values[:16]):
                for x in range(8):
                    rows[y, x] = bool(bits & (0x80 >> x))
            glyphs[code] = rows
    _glyph_cache = glyphs
    return glyphs


def ink_mask(image, threshold=150):
    """Binarise a screenshot: True where a glyph pixel was drawn."""
    rgb = np.asarray(image.convert("RGB"), dtype=np.int32)
    lum = (rgb[:, :, 0] * 299 + rgb[:, :, 1] * 587 + rgb[:, :, 2] * 114) // 1000
    return lum > threshold


def render(text):
    """Render `text` into a (16, 8*len) boolean mask."""
    glyphs = load_font()
    mask = np.zeros((16, 8 * len(text)), dtype=bool)
    for i, ch in enumerate(text):
        glyph = glyphs.get(ord(ch))
        if glyph is None:
            raise ValueError("no glyph for %r" % ch)
        mask[:, i * 8:(i + 1) * 8] = glyph
    return mask


def find_text(image, text, threshold=150, tolerance=6, region=None):
    """Return the (x, y) positions where `text` appears in the screenshot."""
    if not text:
        return []
    ink = ink_mask(image, threshold)
    if region:
        x0, y0, x1, y1 = region
        ink = ink[y0:y1, x0:x1]
    else:
        x0, y0 = 0, 0

    mask = render(text)
    h, w = mask.shape
    H, W = ink.shape
    if H < h or W < w:
        return []

    # Locate candidates with the first glyph, then verify the whole string.
    first = mask[:, :8]
    score = np.zeros((H - h + 1, W - w + 1), dtype=np.int16)
    for dy in range(16):
        for dx in range(8):
            window = ink[dy:dy + H - h + 1, dx:dx + W - w + 1]
            score += (window == first[dy, dx])
    candidates = np.argwhere(score >= 128 - tolerance)

    hits = []
    for y, x in candidates:
        block = ink[y:y + h, x:x + w]
        if np.count_nonzero(block != mask) <= tolerance * len(text):
            hits.append((int(x) + x0, int(y) + y0))
    return hits


def contains(image, text, **kwargs):
    return len(find_text(image, text, **kwargs)) > 0


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    image = Image.open(sys.argv[1])
    for text in sys.argv[2:]:
        hits = find_text(image, text)
        print("%-40r %s" % (text, hits if hits else "NOT FOUND"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
