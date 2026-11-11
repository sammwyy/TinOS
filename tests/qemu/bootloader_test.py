#!/usr/bin/env python3
"""Exercise the BIOS boot menu, kernel self-test and AP trampoline."""

import argparse

from qemu_test import Tinos


def boot_selftest(iso):
    vm = Tinos(iso, mem=16, verbose=False)
    try:
        vm.start()
        vm.key("down")
        vm.key("ret")
        assert vm.wait_for_serial("[test] ---- self-test passed ----",
                                  timeout=35), vm.serial()
        assert "KERNEL PANIC" not in vm.serial()
    finally:
        vm.stop()
        vm.cleanup()


def boot_secondary_cpu(iso):
    vm = Tinos(iso, mem=64, cpus=2, verbose=False)
    try:
        vm.start()
        assert vm.wait_for_serial("[smp ] cpu apic=1 up (2 online)",
                                  timeout=35), vm.serial()
        assert "KERNEL PANIC" not in vm.serial()
    finally:
        vm.stop()
        vm.cleanup()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--iso", default="build/tinos.iso")
    args = parser.parse_args()

    boot_selftest(args.iso)
    boot_secondary_cpu(args.iso)
    print("BIOS boot menu, kernel self-test and AP startup: OK")


if __name__ == "__main__":
    main()
