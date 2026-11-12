#!/usr/bin/env python3
"""Verify CPU-bound processes cannot monopolize the text console."""

import argparse
import time

from qemu_test import Tinos


def wait_count(vm, marker, count, timeout=15):
    deadline = time.time() + timeout
    while vm.serial().count(marker) < count:
        assert time.time() < deadline, vm.serial()
        time.sleep(0.1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--iso", default="build/tinos.iso")
    parser.add_argument("--mem", type=int, default=16)
    parser.add_argument("--cpus", type=int, default=1)
    args = parser.parse_args()

    vm = Tinos(args.iso, mem=args.mem, cpus=args.cpus, verbose=False)
    try:
        vm.start()
        vm.key("down")
        vm.key("down")
        vm.key("ret")
        assert vm.wait_for_serial("[tty] text console ready", timeout=30), vm.serial()
        for i in range(2):
            vm.line("run /apps/spin.elf")
            wait_count(vm, "ring3: spinner started", i + 1)

        vm.line("run /apps/hello.elf")
        assert vm.wait_for_serial("[proc] ring 3 exit status 17",
                                  timeout=15), vm.serial()
        vm.line("kill 1")
        assert vm.wait_for_serial("[proc] killed pid 1", timeout=15), vm.serial()
        vm.line("kill 2")
        assert vm.wait_for_serial("[proc] killed pid 2", timeout=15), vm.serial()

        vm.line("run /apps/echo.elf")
        assert vm.wait_for_serial("ring3: type a line>", timeout=15), vm.serial()
        vm.key("ctrl", "c")
        assert vm.wait_for_serial("[proc] killed pid 4", timeout=15), vm.serial()

        vm.line("run /apps/cat.elf /README.txt")
        assert vm.wait_for_serial("Welcome to TinOS!", timeout=15), vm.serial()
        for i in range(2, 6):
            vm.line("run /apps/hello.elf")
            wait_count(vm, "ring3: hello from an isolated ELF", i)
        assert "KERNEL PANIC" not in vm.serial()
        print(f"{args.mem} MiB, {args.cpus} CPU preemptive processes and TTY: OK")
    finally:
        vm.stop()
        vm.cleanup()


if __name__ == "__main__":
    main()
