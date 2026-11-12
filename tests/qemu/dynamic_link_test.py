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

        chain_marker = "ring3: chained libraries keep private state"
        for count in (1, 2):
            before = len(vm.serial())
            vm.line("run /apps/chainprobe.elf")
            wait_new(vm, before, chain_marker)
            assert vm.serial().count(chain_marker) == count, vm.serial()
        assert vm.serial().count("cached libtinoschain.so read-only pages") == 1
        assert vm.serial().count("cached libtinostate.so read-only pages") == 1
        assert vm.serial().count("reused libtinoschain.so read-only pages") == 1
        assert vm.serial().count("reused libtinostate.so read-only pages") == 1

        before = len(vm.serial())
        vm.line("run /apps/chainopen.elf")
        wait_new(vm, before, "ring3: dlopen resolved a library dependency")
        assert vm.serial().count("reused libtinoschain.so read-only pages") == 2
        assert vm.serial().count("reused libtinostate.so read-only pages") == 2

        before = len(vm.serial())
        vm.line("run /apps/chainclose.elf")
        wait_new(vm, before, "ring3: dlclose released private library state")
        assert vm.serial().count("reused libtinoschain.so read-only pages") == 4
        assert vm.serial().count("reused libtinostate.so read-only pages") == 4

        tls_marker = "ring3: private initial-exec TLS survives preemption"
        for count in (1, 2):
            before = len(vm.serial())
            vm.line("run /apps/tlsprobe.elf")
            wait_new(vm, before, tls_marker)
            assert vm.serial().count(tls_marker) == count, vm.serial()
        assert vm.serial().count("cached libtinostls.so read-only pages") == 1
        assert vm.serial().count("reused libtinostls.so read-only pages") == 3

        gd_marker = "ring3: global-dynamic TLS keeps private state"
        for count in (1, 2):
            before = len(vm.serial())
            vm.line("run /apps/gdprobe.elf")
            wait_new(vm, before, gd_marker)
            assert vm.serial().count(gd_marker) == count, vm.serial()
        assert vm.serial().count("cached libtinosgd.so read-only pages") == 1
        assert vm.serial().count("cached libtinosrt.so read-only pages") == 1
        assert vm.serial().count("reused libtinosgd.so read-only pages") == 3
        assert vm.serial().count("reused libtinosrt.so read-only pages") == 3

        before = len(vm.serial())
        vm.line("run /apps/gd_consumer_probe.elf")
        wait_new(vm, before, "ring3: TLS imports use dependency state")

        before = len(vm.serial())
        vm.line("run /apps/versionprobe.elf")
        wait_new(vm, before, "ring3: versioned ELF imports resolve correctly")

        before = len(vm.serial())
        vm.line("run /apps/versionopen.elf")
        wait_new(vm, before, "ring3: versioned library dependency works")

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
