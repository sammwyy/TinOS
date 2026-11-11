#!/usr/bin/env python3
"""Run isolated ELF32 programs and exercise the syscall boundary."""

import argparse
import time

from qemu_test import Tinos


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--iso", default="build/tinos.iso")
    args = parser.parse_args()

    vm = Tinos(args.iso, mem=16, verbose=False)
    try:
        vm.start()
        vm.key("down")
        vm.key("down")
        vm.key("ret")
        assert vm.wait_for_serial("[tty] text console ready", timeout=30), vm.serial()

        vm.line("run /apps/hello.elf")
        assert vm.wait_for_serial("[proc] ring 3 exit status 17", timeout=15), vm.serial()
        assert "ring3: hello from an isolated ELF" in vm.serial()

        vm.line("run /apps/badptr.elf")
        assert vm.wait_for_serial("ring3: bad pointer rejected", timeout=15), vm.serial()
        assert "[proc] ring 3 exit status 0" in vm.serial()

        vm.line("run /apps/fault.elf")
        assert vm.wait_for_serial("[proc] ring 3 exit status 142", timeout=15), vm.serial()
        assert "[proc] user exception 14" in vm.serial()

        vm.line("run /apps/textwrite.elf")
        deadline = time.time() + 15
        while vm.serial().count("[proc] ring 3 exit status 142") < 2:
            assert time.time() < deadline, vm.serial()
            time.sleep(0.1)

        vm.line("run /apps/io.elf")
        assert vm.wait_for_serial("[proc] ring 3 exit status 141",
                                  timeout=15), vm.serial()
        assert "[proc] user exception 13" in vm.serial()

        vm.line("run /apps/fpu.elf")
        assert vm.wait_for_serial("[proc] ring 3 exit status 135",
                                  timeout=15), vm.serial()
        assert "[proc] user exception 7" in vm.serial()

        vm.line("run /apps/df.elf")
        assert vm.wait_for_serial("ring3: direction flag isolated",
                                  timeout=15), vm.serial()

        vm.line("run /apps/echo.elf")
        assert vm.wait_for_serial("ring3: type a line>", timeout=15), vm.serial()
        vm.line("hello process")
        assert vm.wait_for_serial("ring3 echo: hello process", timeout=15), vm.serial()

        vm.line("run /apps/hello.elf")
        deadline = time.time() + 15
        while vm.serial().count("ring3: hello from an isolated ELF") < 2:
            assert time.time() < deadline, vm.serial()
            time.sleep(0.1)
        assert vm.serial().count("ring3: hello from an isolated ELF") == 2
        assert "KERNEL PANIC" not in vm.serial()
        print("ring 3 ELF, syscalls, fault containment and TTY recovery: OK")
    finally:
        vm.stop()
        vm.cleanup()


if __name__ == "__main__":
    main()
