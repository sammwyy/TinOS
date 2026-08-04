#!/usr/bin/env python3
"""Pin every Rock Ridge "TF" timestamp field in an ISO9660 image.

xorriso's -volume_date/-alter_date options cover creation, modification and
access time, but not the "attribute change" field that grub2-mkrescue's own
module files pick up from their real on-disk ctime (which cannot be set with
touch). That field is the only source of build-to-build ISO variance left
once the volume descriptor and RR modify/access dates are pinned, so this
script walks the finished image and overwrites every timestamp inside every
"TF" SUSP record with a fixed date, regardless of which of the fields the
record's flag byte marks as present.

    tools/pin_iso_dates.py build/tinos.iso
"""

import sys

FIXED_DATE = bytes([100, 1, 1, 0, 0, 0, 0])  # 2000-01-01 00:00:00 GMT+0


def pin(data: bytearray) -> int:
    patched = 0
    i = 0
    limit = len(data) - 5
    while i < limit:
        if data[i:i + 2] == b"TF" and data[i + 3] == 1:
            length = data[i + 2]
            flags = data[i + 4]
            long_form = bool(flags & 0x80)
            field_size = 17 if long_form else 7
            count = bin(flags & 0x7F).count("1")
            expected = 5 + count * field_size
            if length == expected and 5 <= length <= 5 + 7 * field_size:
                off = i + 5
                for _ in range(count):
                    if not long_form:
                        data[off:off + 7] = FIXED_DATE
                    off += field_size
                patched += 1
                i += length
                continue
        i += 1
    return patched


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: pin_iso_dates.py <iso>", file=sys.stderr)
        return 2
    path = sys.argv[1]
    with open(path, "rb") as f:
        data = bytearray(f.read())
    count = pin(data)
    with open(path, "wb") as f:
        f.write(data)
    print("pinned %d Rock Ridge timestamp records" % count)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
