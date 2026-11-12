#!/usr/bin/env python3
"""Exercise userspace spawn, wait, ownership and orphan reaping."""

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
    args = parser.parse_args()

    vm = Tinos(args.iso, mem=args.mem, verbose=False)
    try:
        vm.start()
        vm.key("down")
        vm.key("down")
        vm.key("ret")
        assert vm.wait_for_serial("[tty] text console ready", timeout=30), vm.serial()

        vm.line("run /apps/spawn_badarg.elf")
        assert vm.wait_for_serial("ring3: invalid spawn arguments rejected",
                                  timeout=15), vm.serial()
        vm.line("run /apps/multi.elf")
        assert vm.wait_for_serial("ring3: both children collected",
                                  timeout=15), vm.serial()
        wait_count(vm, "ring3: hello from an isolated ELF", 2)

        vm.line("run /apps/spawn.elf /apps/hello.elf")
        wait_count(vm, "ring3: parent collected child", 1)
        wait_count(vm, "ring3: hello from an isolated ELF", 3)

        vm.line("run /apps/spawn.elf /apps/spin.elf 130")
        wait_count(vm, "ring3: parent waiting for child", 2)
        wait_count(vm, "ring3: spinner started", 1)
        vm.line("kill 8")
        assert vm.wait_for_serial("[proc] killed pid 8", timeout=15), vm.serial()
        wait_count(vm, "ring3: parent collected child", 2)

        vm.line("run /apps/spawn.elf /apps/spin.elf 130")
        wait_count(vm, "ring3: spinner started", 2)
        vm.line("kill 9")
        assert vm.wait_for_serial("[proc] killed pid 9", timeout=15), vm.serial()
        vm.line("kill 10")
        assert vm.wait_for_serial("[proc] killed pid 10", timeout=15), vm.serial()
        vm.line("reap 10")
        vm.line("run /apps/hello.elf")
        wait_count(vm, "ring3: hello from an isolated ELF", 4)
        assert "KERNEL PANIC" not in vm.serial()
        print(f"{args.mem} MiB userspace spawn, wait and orphan cleanup: OK")
    finally:
        vm.stop()
        vm.cleanup()


if __name__ == "__main__":
    main()
