#!/usr/bin/env python3
"""Boot altered initrds and ensure corrupt shared objects fail without a panic."""

import argparse
import io
import pathlib
import struct
import subprocess
import tarfile
import tempfile
import time

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

def invalid_tls_import(image):
    headers = list(program_headers(image))
    dynamic = next(ph for ph in headers if ph[0] == 2)
    rel = next(value for tag, value in struct.iter_unpack(
        "<II", image[dynamic[1]:dynamic[1] + dynamic[4]]) if tag == 17)
    segment = next(ph for ph in headers if ph[0] == 1 and
                   ph[2] <= rel and rel - ph[2] + 8 <= ph[4])
    offset = segment[1] + rel - segment[2]
    info = struct.unpack_from("<I", image, offset + 4)[0]
    assert info & 255 == 35  # R_386_TLS_DTPMOD32
    struct.pack_into("<I", image, offset + 4, 128 << 8 | 35)
    return image

def version_definition(image, missing):
    headers = list(program_headers(image))
    dynamic = next(ph for ph in headers if ph[0] == 2)
    verdef = next(value for tag, value in struct.iter_unpack(
        "<II", image[dynamic[1]:dynamic[1] + dynamic[4]])
        if tag == 0x6ffffffc)
    segment = next(ph for ph in headers if ph[0] == 1 and
                   ph[2] <= verdef and verdef - ph[2] + 20 <= ph[4])
    first = segment[1] + verdef - segment[2]
    second = first + struct.unpack_from("<I", image, first + 16)[0]
    assert struct.unpack_from("<H", image, second + 4)[0] == 2
    if not missing:
        struct.pack_into("<I", image, second + 12, 0xfffffff0)
        return image
    before, after = b"TINOS_1\x00", b"TINOS_9\x00"
    assert image.count(before) >= 1 and len(before) == len(after)
    image[:] = image.replace(before, after)
    hash_value = 0
    for ch in b"TINOS_9":
        hash_value = (hash_value << 4) + ch
        high = hash_value & 0xf0000000
        if high:
            hash_value ^= high >> 24
        hash_value &= ~high
    struct.pack_into("<I", image, second + 8, hash_value)
    return image

def invalid_version_requirement(image):
    headers = list(program_headers(image))
    dynamic = next(ph for ph in headers if ph[0] == 2)
    verneed = next(value for tag, value in struct.iter_unpack(
        "<II", image[dynamic[1]:dynamic[1] + dynamic[4]])
        if tag == 0x6ffffffe)
    segment = next(ph for ph in headers if ph[0] == 1 and
                   ph[2] <= verneed and verneed - ph[2] + 16 <= ph[4])
    offset = segment[1] + verneed - segment[2]
    struct.pack_into("<I", image, offset + 8, 0xfffffff0)
    return image


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
            elif (member.name == "lib/libtinosconsumer.so" and
                  mode == "tls_import"):
                data = invalid_tls_import(bytearray(data))
            elif (member.name == "lib/libtinosversioned.so" and
                  mode in ("version", "version_missing")):
                data = version_definition(bytearray(data),
                                          mode == "version_missing")
            elif member.name == "versionprobe.elf" and mode == "version_need":
                data = invalid_version_requirement(bytearray(data))
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
        if mode in ("version", "version_missing", "version_need"):
            vm.line("run /apps/versionprobe.elf")
            if mode == "version":
                assert vm.wait_for_serial(
                    "[ld  ] rejected libtinosversioned.so",
                    timeout=15), vm.serial()
            else:
                if mode == "version_missing":
                    assert vm.wait_for_serial(
                        "[ld  ] cached libtinosversioned.so",
                        timeout=15), vm.serial()
                time.sleep(1)
            assert "ring3: versioned ELF imports resolve correctly" not in vm.serial()
            assert "[proc] spawned pid" not in vm.serial()
        elif mode in ("tls", "tls_import"):
            library = ("libtinostls.so" if mode == "tls" else
                       "libtinosconsumer.so")
            program = ("tlsprobe.elf" if mode == "tls" else
                       "gd_consumer_probe.elf")
            vm.line("run /apps/" + program)
            assert vm.wait_for_serial("[ld  ] rejected " + library,
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
    for mode in ("relocation", "dependency", "initializer", "hash", "tls",
                 "tls_import", "version", "version_missing", "version_need"):
        with tempfile.TemporaryDirectory(prefix="tinos-invalid-so-") as path:
            boot_variant(args.initrd, pathlib.Path(args.kernel),
                         pathlib.Path(args.loader), mode, pathlib.Path(path))
    print("invalid library relocation, dependency, initializer, GNU hash, "
          "TLS and version metadata rejected: OK")


if __name__ == "__main__":
    main()
