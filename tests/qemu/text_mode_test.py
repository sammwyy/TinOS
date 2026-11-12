#!/usr/bin/env python3
"""Boot the text console, execute a command and start the desktop."""

import argparse
import time

from qemu_test import Tinos


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--iso", default="build/tinos.iso")
    parser.add_argument("--mem", type=int, default=256)
    parser.add_argument("--vga", default="std")
    parser.add_argument("--boot-default", action="store_true")
    parser.add_argument("--no-desktop", action="store_true")
    parser.add_argument("--retry-desktop", action="store_true")
    args = parser.parse_args()

    vm = Tinos(args.iso, mem=args.mem, vga=args.vga, verbose=False)
    try:
        vm.start()
        if not args.boot_default:
            vm.key("down")
            vm.key("down")
            vm.key("ret")
        assert vm.wait_for_serial("[tty] text console ready", timeout=30), vm.serial()
        assert "graphics mode active" not in vm.serial()
        assert "KERNEL PANIC" not in vm.serial()
        if args.mem == 16:
            assert "userland: imported 14 ELF32 file(s)" in vm.serial()

        vm.line("pwd")
        time.sleep(0.3)
        assert "graphics mode active" not in vm.serial()

        if not args.no_desktop:
            vm.line("desktop")
            assert vm.wait_for_serial("desktop starting", timeout=30), vm.serial()
        if args.retry_desktop:
            vm.line("desktop")
            deadline = time.time() + 20
            while time.time() < deadline:
                if vm.serial().count("[tty] text console ready") >= 2:
                    break
                time.sleep(0.25)
            assert vm.serial().count("[tty] text console ready") >= 2, vm.serial()
            assert "desktop starting" not in vm.serial()
        assert "KERNEL PANIC" not in vm.serial()
        print("%d MiB text boot and shell%s: OK" % (
            args.mem, "" if args.no_desktop else " to desktop"))
    finally:
        vm.stop()
        vm.cleanup()


if __name__ == "__main__":
    main()
