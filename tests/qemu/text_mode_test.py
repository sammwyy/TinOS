#!/usr/bin/env python3
"""Boot the text console, execute a command and start the desktop."""

import argparse
import time

from qemu_test import Tinos


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--iso", default="build/tinos.iso")
    args = parser.parse_args()

    vm = Tinos(args.iso, verbose=False)
    try:
        vm.start()
        vm.key("down")
        vm.key("down")
        vm.key("ret")
        assert vm.wait_for_serial("[tty] text console ready", timeout=30), vm.serial()
        assert "graphics mode active" not in vm.serial()

        vm.line("pwd")
        time.sleep(0.3)
        assert "graphics mode active" not in vm.serial()

        vm.line("desktop")
        assert vm.wait_for_serial("desktop starting", timeout=30), vm.serial()
        assert "KERNEL PANIC" not in vm.serial()
        print("text boot, shell input and desktop launch: OK")
    finally:
        vm.stop()
        vm.cleanup()


if __name__ == "__main__":
    main()
