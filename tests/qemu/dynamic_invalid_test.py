#!/usr/bin/env python3
"""Boot altered initrds and ensure corrupt shared objects fail without a panic."""

import argparse
import io
import pathlib
import struct
import subprocess
import tarfile
import tempfile

from qemu_test import Tinos


def program_headers(image):
    phoff = struct.unpack_from("<I", image, 28)[0]
    phsize, count = struct.unpack_from("<HH", image, 42)
    assert phsize == 32 and phoff + phsize * count <= len(image)
    for i in range(count):
        yield struct.unpack_from("<IIIIIIII", image, phoff + i * phsize)


def invalid_relocation(image):
    headers = list(program_headers(image))
    dynamic = next(ph for ph in headers if ph[0] == 2)
    jmprel = next(value for tag, value in struct.iter_unpack(
        "<II", image[dynamic[1]:dynamic[1] + dynamic[4]]) if tag == 23)
    segment = next(ph for ph in headers if ph[0] == 1 and
                   ph[2] <= jmprel and jmprel - ph[2] + 8 <= ph[4])
    offset = segment[1] + jmprel - segment[2]
    assert image[offset + 4] == 7  # R_386_JUMP_SLOT
    struct.pack_into("<I", image, offset, 0xc0100000)
    return image


def missing_dependency(image):
    before = b"libtinostate.so"
    after = b"libtinobogus.so"
    assert len(before) == len(after) and image.count(before) >= 1
    return image.replace(before, after)

def invalid_initializer(image):
    dynamic = next(ph for ph in program_headers(image) if ph[0] == 2)
    for i in range(dynamic[4] // 8):
        entry = dynamic[1] + i * 8
        if struct.unpack_from("<I", image, entry)[0] == 27:
            struct.pack_into("<I", image, entry + 4, 4096)
            return image
    raise AssertionError("library has no initializer array")

def invalid_gnu_hash(image):
    headers = list(program_headers(image))
    dynamic = next(ph for ph in headers if ph[0] == 2)
    gnu_hash = next(value for tag, value in struct.iter_unpack(
        "<II", image[dynamic[1]:dynamic[1] + dynamic[4]])
        if tag == 0x6ffffef5)
    segment = next(ph for ph in headers if ph[0] == 1 and
                   ph[2] <= gnu_hash and gnu_hash - ph[2] + 16 <= ph[4])
    offset = segment[1] + gnu_hash - segment[2]
    struct.pack_into("<I", image, offset, 0xffffffff)
    return image

def invalid_tls(image):
    phoff = struct.unpack_from("<I", image, 28)[0]
    phsize, count = struct.unpack_from("<HH", image, 42)
    for i in range(count):
        entry = phoff + i * phsize
        if struct.unpack_from("<I", image, entry)[0] == 7:
            struct.pack_into("<I", image, entry + 20, 4097)
            return image
    raise AssertionError("library has no PT_TLS")


def boot_variant(source, kernel, loader, mode, temp):
    archive_path = temp / "ramfs.tar"
    with tarfile.open(source) as archive, tarfile.open(
            archive_path, "w", format=tarfile.USTAR_FORMAT) as output:
        for member in archive:
            data = archive.extractfile(member).read()
            if member.name == "lib/libtinostate.so" and mode == "hash":
                data = invalid_gnu_hash(bytearray(data))
            elif member.name == "lib/libtinostls.so" and mode == "tls":
                data = invalid_tls(bytearray(data))
            elif member.name == "lib/libtinoschain.so" and mode in (
                    "relocation", "dependency", "initializer"):
                modified = bytearray(data)
                if mode == "relocation":
                    data = invalid_relocation(modified)
                elif mode == "dependency":
                    data = missing_dependency(modified)
                else:
                    data = invalid_initializer(modified)
            output.addfile(member, io.BytesIO(data))

    image = temp / "tinos-boot.img"
    subprocess.run(["python3", "boot/x86/build_image.py", str(kernel),
                    str(archive_path), str(loader), str(image)], check=True)
    iso_tree = temp / "iso" / "boot"
    iso_tree.mkdir(parents=True)
    (iso_tree / "tinos-boot.img").write_bytes(image.read_bytes())
    iso = temp / "invalid.iso"
    subprocess.run([
        "xorriso", "-as", "mkisofs", "-o", str(iso), "-volid", "TINOS",
        "--modification-date=2000010100000000",
        "--set_all_file_dates", "2000010100000000",
        "-b", "boot/tinos-boot.img", "-no-emul-boot", "-boot-load-size", "4",
        "-boot-info-table", str(temp / "iso"),
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    vm = Tinos(str(iso), mem=16, verbose=False)
    try:
        vm.start()
        vm.key("down")
        vm.key("down")
        vm.key("ret")
        assert vm.wait_for_serial("[tty] text console ready", timeout=30), vm.serial()
        if mode == "tls":
            vm.line("run /apps/tlsprobe.elf")
            assert vm.wait_for_serial("[ld  ] rejected libtinostls.so",
                                      timeout=15), vm.serial()
            assert vm.wait_for_serial("[proc] ring 3 exit status 1",
                                      timeout=15), vm.serial()
        else:
            vm.line("run /apps/chainprobe.elf")
            assert vm.wait_for_serial("[ld  ] rejected libtinoschain.so",
                                      timeout=15), vm.serial()
            assert "ring3: chained libraries keep private state" not in vm.serial()
            assert "[proc] spawned pid" not in vm.serial()
        vm.line("run /apps/hello.elf")
        assert vm.wait_for_serial("ring3: hello from an isolated ELF",
                                  timeout=15), vm.serial()
        assert "KERNEL PANIC" not in vm.serial()
    finally:
        vm.stop()
        vm.cleanup()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--kernel", default="build/tinos.elf")
    parser.add_argument("--initrd", default="build/ramfs.tar")
    parser.add_argument("--loader", default="boot/x86/loader.asm")
    args = parser.parse_args()
    for mode in ("relocation", "dependency", "initializer", "hash", "tls"):
        with tempfile.TemporaryDirectory(prefix="tinos-invalid-so-") as path:
            boot_variant(args.initrd, pathlib.Path(args.kernel),
                         pathlib.Path(args.loader), mode, pathlib.Path(path))
    print("invalid library relocation, dependency, initializer, GNU hash "
          "and TLS rejected: OK")


if __name__ == "__main__":
    main()
