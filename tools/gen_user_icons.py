#!/usr/bin/env python3
"""Extract the desktop's bitmap icons into a small userspace icon set."""

from pathlib import Path
import sys

from gen_icons import read_bmp


def main():
    source = Path(sys.argv[1])
    target = Path(sys.argv[2])
    files = sorted(source.glob("*.bmp"))
    lines = ["/* Generated from resources/icons. Do not edit. */",
             "#include <stdint.h>", "",
             "const char *const tinos_ui_icon_names[] = {"]
    lines.extend(f'    "{path.stem}",' for path in files)
    lines += ["};", f"const unsigned tinos_ui_icon_count = {len(files)};",
              "const uint32_t tinos_ui_icons[][256] = {"]
    for path in files:
        width, height, pixels = read_bmp(path)
        lines.append("    { /* " + path.stem + " */")
        for y in range(16):
            row = [pixels[(y * height // 16) * width + x * width // 16]
                   for x in range(16)]
            lines.append("        " + ", ".join(f"0x{value:08x}u" for value in row) + ",")
        lines.append("    },")
    lines += ["};", "const uint32_t tinos_ui_logo[44 * 44] = {"]
    width, height, pixels = read_bmp(source / "tinos.bmp")
    for y in range(44):
        row = [pixels[(y * height // 44) * width + x * width // 44]
               for x in range(44)]
        lines.append("    " + ", ".join(f"0x{value:08x}u" for value in row) + ",")
    lines += ["};", ""]
    target.write_text("\n".join(lines))


if __name__ == "__main__":
    main()
