#!/usr/bin/env python3
"""Build a deterministic RGBA icon atlas and its C metadata table.

Accepted input: uncompressed Windows BMP, 24 or 32 bits per pixel.
"""

from pathlib import Path
import struct
import sys


def fail(message):
    raise SystemExit(f"gen_icons.py: {message}")


def read_bmp(path):
    data = path.read_bytes()
    if len(data) < 54 or data[:2] != b"BM":
        fail(f"{path}: not a BMP file")
    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    header_size = struct.unpack_from("<I", data, 14)[0]
    if header_size < 40:
        fail(f"{path}: unsupported BMP header")
    width, height = struct.unpack_from("<ii", data, 18)
    planes, bpp, compression = struct.unpack_from("<HHI", data, 26)
    # ImageMagick commonly emits 32-bit BMPs with BI_BITFIELDS (the channel
    # masks still describe the usual BGRA byte layout used below).
    if planes != 1 or bpp not in (24, 32) or compression not in (0, 3):
        fail(f"{path}: need an uncompressed 24/32-bit BMP")
    if width <= 0 or height == 0:
        fail(f"{path}: invalid dimensions")

    top_down = height < 0
    height = abs(height)
    stride = ((width * (bpp // 8) + 3) // 4) * 4
    if pixel_offset + stride * height > len(data):
        fail(f"{path}: truncated pixel data")

    pixels = []
    for y in range(height):
        source_y = y if top_down else height - 1 - y
        row = pixel_offset + source_y * stride
        for x in range(width):
            pos = row + x * (bpp // 8)
            b, g, r = data[pos:pos + 3]
            alpha = data[pos + 3] if bpp == 32 else 255
            pixels.append((alpha << 24) | (r << 16) | (g << 8) | b)
    return width, height, pixels


def c_bytes(values, indent="    "):
    lines = []
    for start in range(0, len(values), 24):
        lines.append(indent + ",".join(str(x) for x in values[start:start + 24]) + ",")
    return "\n".join(lines) or indent + "0,"


def c_words(values, indent="    "):
    lines = []
    for start in range(0, len(values), 8):
        lines.append(indent + ", ".join(f"0x{x:08x}u" for x in values[start:start + 8]) + ",")
    return "\n".join(lines) or indent + "0u,"


def quantize(pixels, colors=255):
    """Median-cut every visible ARGB pixel into at most `colors` entries.

    Index 0 is reserved for fully transparent pixels, so the palette holds
    `colors` + 1 words.  The icon art uses flat fills and short gradients,
    so 255 shared entries are visually lossless at 16-48 px."""
    counts = {}
    for p in pixels:
        if p >> 24:
            counts[p] = counts.get(p, 0) + 1
    entries = list(counts.items())

    def channels(p):
        return ((p >> 24) & 255, (p >> 16) & 255, (p >> 8) & 255, p & 255)

    boxes = [entries]
    while len(boxes) < colors:
        best, best_range, axis = None, 0, 0
        for i, box in enumerate(boxes):
            if len(box) < 2:
                continue
            for a in range(4):
                values = [channels(p)[a] for p, _ in box]
                spread = (max(values) - min(values)) * (2 if a == 0 else 1)
                if spread > best_range:
                    best, best_range, axis = i, spread, a
        if best is None:
            break
        box = sorted(boxes.pop(best), key=lambda e: channels(e[0])[axis])
        total = sum(n for _, n in box)
        acc, cut = 0, 1
        for j, (_, n) in enumerate(box):
            acc += n
            if acc * 2 >= total:
                cut = max(1, min(len(box) - 1, j + 1))
                break
        boxes += [box[:cut], box[cut:]]

    palette = [0]
    lookup = {}
    for box in boxes:
        weight = sum(n for _, n in box)
        avg = [sum(channels(p)[a] * n for p, n in box) / weight for a in range(4)]
        word = (round(avg[0]) << 24) | (round(avg[1]) << 16) | \
               (round(avg[2]) << 8) | round(avg[3])
        for p, _ in box:
            lookup[p] = len(palette)
        palette.append(word)
    return palette, [lookup[p] if p >> 24 else 0 for p in pixels]


def main(argv):
    if len(argv) != 3:
        fail("usage: gen_icons.py <input-dir> <output.c>")
    input_dir, output_c = Path(argv[1]), Path(argv[2])
    output_h = output_c.with_suffix(".h")
    files = sorted(input_dir.glob("*.bmp"), key=lambda p: p.name.lower()) if input_dir.exists() else []

    # <name>.bmp and <name>@<size>.bmp are size variants of one icon.
    icons = []
    seen = set()
    for path in files:
        name, _, _ = path.stem.partition("@")
        w, h, pixels = read_bmp(path)
        if not name or (name, w) in seen:
            fail(f"duplicate/empty icon: {path.name}")
        if w != h or w > 64:
            fail(f"{path.name}: icons must be square and at most 64 px")
        seen.add((name, w))
        icons.append((name, w, pixels))
    icons.sort(key=lambda icon: (icon[0], icon[1]))

    max_width = 256
    atlas_w = atlas_h = 0
    placements = []
    shelf_x = shelf_y = shelf_h = 0
    for name, w, pixels in sorted(icons, key=lambda icon: -icon[1]):
        if shelf_x and shelf_x + w > max_width:
            shelf_y += shelf_h
            shelf_x = shelf_h = 0
        placements.append((name, w, pixels, shelf_x, shelf_y))
        shelf_x += w
        shelf_h = max(shelf_h, w)
        atlas_w = max(atlas_w, shelf_x)
        atlas_h = max(atlas_h, shelf_y + w)
    placements.sort(key=lambda p: (p[0], p[1]))

    atlas = [0] * max(1, atlas_w * atlas_h)
    for name, w, pixels, x0, y0 in placements:
        for y in range(w):
            atlas[(y0 + y) * atlas_w + x0:(y0 + y) * atlas_w + x0 + w] = pixels[y * w:(y + 1) * w]
    palette, indices = quantize(atlas)
    palette += [0] * (256 - len(palette))

    output_h.parent.mkdir(parents=True, exist_ok=True)
    header = """/* Generated by tools/gen_icons.py. Do not edit. */
#ifndef TINOS_GENERATED_ICONS_H
#define TINOS_GENERATED_ICONS_H
#include <tinos/types.h>

/* One square size variant of a named icon; variants of the same name are
 * adjacent and sorted by ascending size. Pixels are 8-bit indices into
 * custom_icon_palette (ARGB, index 0 transparent). */
typedef struct {
    const char *name;
    uint16_t x, y, size;
} custom_icon_t;

extern const uint8_t custom_icon_atlas[];
extern const uint32_t custom_icon_palette[256];
extern const uint16_t custom_icon_atlas_width;
extern const uint16_t custom_icon_atlas_height;
extern const custom_icon_t custom_icons[];
extern const size_t custom_icon_count;

#endif
"""

    lines = ["/* Generated by tools/gen_icons.py. Do not edit. */", '#include "icons_generated.h"', "",
             f"const uint16_t custom_icon_atlas_width = {atlas_w};",
             f"const uint16_t custom_icon_atlas_height = {atlas_h};",
             "const uint32_t custom_icon_palette[256] = {", c_words(palette), "};", "",
             "const uint8_t custom_icon_atlas[] = {", c_bytes(indices), "};", "",
             "const custom_icon_t custom_icons[] = {"]
    for name, w, pixels, x, y in placements:
        lines.append(f'    {{ "{name}", {x}, {y}, {w} }},')
    if not placements:
        lines.append("    { 0, 0, 0, 0 },")
    lines += ["};", f"const size_t custom_icon_count = {len(placements)};", ""]
    source = "\n".join(lines)
    # Keep mtimes stable when resources did not change, so an ordinary
    # incremental build does not recompile the kernel for no reason.
    if not output_h.exists() or output_h.read_text() != header:
        output_h.write_text(header)
    if not output_c.exists() or output_c.read_text() != source:
        output_c.write_text(source)


if __name__ == "__main__":
    main(sys.argv)
