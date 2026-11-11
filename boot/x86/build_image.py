#!/usr/bin/env python3
"""Package the BIOS loader, kernel image, initrd and AP trampoline."""

import argparse
import pathlib
import subprocess

SECTOR = 2048


def symbols(kernel):
    output = subprocess.check_output(["nm", "-n", str(kernel)], text=True)
    return {parts[2]: int(parts[0], 16) for line in output.splitlines()
            if len(parts := line.split()) == 3}


def sector_pad(data):
    return data + bytes((-len(data)) % SECTOR)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("kernel", type=pathlib.Path)
    parser.add_argument("initrd", type=pathlib.Path)
    parser.add_argument("loader", type=pathlib.Path)
    parser.add_argument("image", type=pathlib.Path)
    args = parser.parse_args()

    addresses = symbols(args.kernel)
    assert addresses["__kernel_start"] == 0x100000
    assert addresses["__kernel_end"] < 0x600000

    kernel_binary = args.image.with_suffix(".kernel.bin")
    ap_binary = args.image.with_suffix(".ap.bin")
    subprocess.run(["objcopy", "-O", "binary", "--remove-section=.trampoline",
                    str(args.kernel), str(kernel_binary)], check=True)
    subprocess.run(["objcopy", "-O", "binary", "--only-section=.trampoline",
                    str(args.kernel), str(ap_binary)], check=True)

    payload = sector_pad(kernel_binary.read_bytes())
    initrd = sector_pad(args.initrd.read_bytes())
    trampoline = sector_pad(ap_binary.read_bytes())
    assert len(trampoline) == SECTOR

    initrd_address = addresses["__kernel_end"]
    assert 0x100000 + len(payload) <= initrd_address
    assert len(payload) // SECTOR <= 0xffff
    assert len(initrd) // SECTOR <= 0xffff
    defines = {
        "KERNEL_SECTORS": len(payload) // SECTOR,
        "INITRD_SECTORS": len(initrd) // SECTOR,
        "INITRD_ADDR": initrd_address,
        "INITRD_SIZE": args.initrd.stat().st_size,
        "BSS_START": addresses["__bss_start"],
        "BSS_END": addresses["__bss_end"],
        "KERNEL_ENTRY": addresses["_start"],
    }
    command = ["nasm", "-f", "bin"]
    command += ["-D%s=%d" % (key, value) for key, value in defines.items()]
    command += [str(args.loader), "-o", str(args.image.with_suffix(".stage.bin"))]
    subprocess.run(command, check=True)
    stage = args.image.with_suffix(".stage.bin").read_bytes()
    assert len(stage) == SECTOR
    args.image.write_bytes(stage + payload + initrd + trampoline)


if __name__ == "__main__":
    main()
