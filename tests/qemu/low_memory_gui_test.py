#!/usr/bin/env python3
"""Exercise the 640x480 graphical session with 8 MiB of RAM."""

import argparse
import os

from qemu_test import Screen, Tinos


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--iso", default="build/tinos.iso")
    args = parser.parse_args()

    vm = Tinos(args.iso, mem=8, verbose=False)
    try:
        vm.start()
        assert vm.wait_for_serial("desktop starting", timeout=30), vm.serial()
        assert "apps: loaded 9 native app(s)" in vm.serial(), vm.serial()
        assert "mode set to 640x480x32" in vm.serial(), vm.serial()
        vm.key("ctrl", "alt", "t")
        assert vm.wait_for_serial("opened window 1 'Terminal", timeout=15), vm.serial()
        assert "(640x452)" in vm.serial(), vm.serial()

        path = os.path.join(vm.tmp, "terminal.png")
        vm.screenshot(path)
        screen = Screen(path)
        assert screen.size == (640, 480), screen.size
        assert screen.has("Terminal"), "terminal title not visible"
        assert "KERNEL PANIC" not in vm.serial()
        print("8 MiB desktop, app loading and window layout: OK")
    finally:
        vm.stop()
        vm.cleanup()


if __name__ == "__main__":
    main()
