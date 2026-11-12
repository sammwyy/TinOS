#!/usr/bin/env python3
"""Ensure the boot image cannot load the retired ring-0 application ABI."""

import argparse
import struct
import subprocess
import tarfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--kernel", default="build/tinos.elf")
    parser.add_argument("--initrd", default="build/ramfs.tar")
    args = parser.parse_args()

    with tarfile.open(args.initrd) as archive:
        names = archive.getnames()
        assert names and "hello.elf" in names, names
        assert "media/demo.bmp" in names, names
        assert "media/demo.wav" in names, names
        libraries = ("lib/libtinosbmp.so", "lib/libtinoswav.so",
                     "lib/libtinostate.so", "lib/libtinoschain.so",
                     "lib/libtinostls.so")
        for library in libraries:
            assert library in names, names
            header = archive.extractfile(library).read(20)
            assert header[:4] == b"\x7fELF" and \
                   struct.unpack_from("<H", header, 16)[0] == 3, library
        assert archive.extractfile("media/demo.bmp").read(2) == b"BM"
        assert archive.extractfile("media/demo.wav").read(4) == b"RIFF"
        assert all(member.isfile() and (
            member.name in (*libraries, "media/demo.bmp", "media/demo.wav") or
            ("/" not in member.name or member.name.startswith("gui/")) and
            member.name.endswith(".elf")) for member in archive), names
    symbols = subprocess.check_output(["nm", "-g", args.kernel], text=True)
    for forbidden in ("tinos_abi_build", "load_native_apps", "apps_register"):
        assert forbidden not in symbols, forbidden
    print("boot image contains ring-3 ELF programs, shared objects, media "
          "and no native ABI: OK")


if __name__ == "__main__":
    main()
