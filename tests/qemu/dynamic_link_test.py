#!/usr/bin/env python3
"""Check ELF32 shared objects, runtime lookup and read-only code sharing."""

import argparse
import time

from qemu_test import Tinos


def wait_new(vm, before, marker):
    deadline = time.time() + 15
    while marker not in vm.serial()[before:]:
        assert time.time() < deadline, vm.serial()
        time.sleep(0.1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--iso", default="build/tinos.iso")
    parser.add_argument("--mem", type=int, default=8)
    args = parser.parse_args()

    vm = Tinos(args.iso, mem=args.mem, verbose=False)
    try:
        vm.start()
        vm.key("down")
        vm.key("down")
        vm.key("ret")
        assert vm.wait_for_serial("[tty] text console ready", timeout=30), vm.serial()
        marker = "ring3: dlopen, dlsym and library isolation OK"

        before = len(vm.serial())
        vm.line("run /apps/libprobe.elf")
        wait_new(vm, before, marker)
        assert vm.serial().count("cached libtinosbmp.so read-only pages") == 1
        assert vm.serial().count("cached libtinoswav.so read-only pages") == 1

        before = len(vm.serial())
        vm.line("run /apps/libprobe.elf")
        wait_new(vm, before, marker)
        assert vm.serial().count("reused libtinosbmp.so read-only pages") == 1
        assert vm.serial().count("reused libtinoswav.so read-only pages") == 1

        before = len(vm.serial())
        vm.line("run /apps/libtamper.elf")
        wait_new(vm, before, "[proc] user exception 14")
        wait_new(vm, before, "[proc] ring 3 exit status 142")

        before = len(vm.serial())
        vm.line("run /apps/libprobe.elf")
        wait_new(vm, before, marker)
        assert "KERNEL PANIC" not in vm.serial()
        print(f"{args.mem} MiB dynamic ELF linking, shared code and "
              "write protection: OK")
    finally:
        vm.stop()
        vm.cleanup()


if __name__ == "__main__":
    main()
